#!/usr/bin/env python3
"""Agent-bridge client for Open Agentic XRay. Protocol v1 (see
docs/superpowers/specs/2026-07-04-agent-bridge-design.md).

TARGET selects one game instance:
  <path>/agent_bridge.sock   Unix domain socket (macOS/Linux engine)
  tcp:HOST:PORT              TCP loopback (Windows engine), e.g. tcp:127.0.0.1:47000
  <path>/agent_bridge.port   port file the Windows engine writes on startup
  <appdata dir>              directory holding agent_bridge.port (or agent_bridge.sock)

Options (before TARGET):
  --wait SECONDS     keep retrying until the instance accepts (port file may not exist yet)
  --timeout SECONDS  per-request socket timeout (default 15)
"""
import socket
import stat
import sys
import time
from pathlib import Path

PORT_FILE = "agent_bridge.port"
SOCK_FILE = "agent_bridge.sock"


class EndpointError(ValueError):
    pass


def parse_port(text: str) -> int:
    text = text.strip()
    if not text.isdigit() or not 0 < int(text) < 65536:
        raise EndpointError(f"invalid port: {text!r}")
    return int(text)


def read_port_file(path: Path) -> int:
    try:
        lines = path.read_text(encoding="ascii").splitlines()
    except (OSError, UnicodeDecodeError) as e:
        raise EndpointError(f"cannot read port file {path}: {e}") from e
    if not lines:
        raise EndpointError(f"empty port file: {path}")
    return parse_port(lines[0])


def _is_socket(path: Path) -> bool:
    try:
        return stat.S_ISSOCK(path.stat().st_mode)
    except OSError:
        return False


def resolve_endpoint(target: str):
    """Map TARGET to ("unix", path) or ("tcp", (host, port)). Raises EndpointError."""
    if target.startswith("tcp:"):
        host, sep, port = target[4:].rpartition(":")
        host = host.strip("[]")
        if not sep or not host:
            raise EndpointError(f"expected tcp:HOST:PORT, got {target!r}")
        return "tcp", (host, parse_port(port))
    path = Path(target)
    if path.is_dir():
        if (path / PORT_FILE).is_file():
            return "tcp", ("127.0.0.1", read_port_file(path / PORT_FILE))
        if _is_socket(path / SOCK_FILE):
            return "unix", str(path / SOCK_FILE)
        raise EndpointError(f"no {PORT_FILE} or {SOCK_FILE} in {path}")
    if path.is_file():  # regular file: a port file (sockets are not regular files)
        return "tcp", ("127.0.0.1", read_port_file(path))
    if path.suffix == ".port":
        raise EndpointError(f"port file not found: {path}")
    return "unix", target


def connect(target: str, timeout: float = 15.0) -> socket.socket:
    kind, address = resolve_endpoint(target)
    if kind == "tcp":
        sock = socket.create_connection(address, timeout=timeout)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        return sock
    if not hasattr(socket, "AF_UNIX"):
        raise EndpointError(f"{target!r} is a Unix socket path but this Python has no AF_UNIX; "
                            "on Windows pass tcp:HOST:PORT, an agent_bridge.port file or the appdata dir")
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect(address)
    except BaseException:
        sock.close()
        raise
    return sock


class Bridge:
    def __init__(self, path: str, timeout: float = 15.0, wait: float = 0.0):
        deadline = time.monotonic() + wait
        while True:
            try:
                self.sock = connect(path, timeout)
                break
            except (OSError, EndpointError):
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.25)
        self.buf = b""
        self.next_id = 1

    def close(self):
        self.sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def request(self, verb: str, payload: str = "") -> tuple[bool, str]:
        rid = str(self.next_id)
        self.next_id += 1
        line = f"{rid} {verb} {payload}".strip() + "\n"
        self.sock.sendall(line.encode())
        while True:
            nl = self.buf.find(b"\n")
            if nl >= 0:
                resp = self.buf[:nl].decode()
                self.buf = self.buf[nl + 1:]
                parts = resp.split(" ", 2)
                if parts[0] != rid:
                    continue  # stale response from a previous client — skip
                ok = len(parts) > 1 and parts[1] == "ok"
                payload_out = parts[2] if len(parts) > 2 else ""
                return ok, payload_out.replace("\\n", "\n")
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("bridge closed the connection")
            self.buf += chunk


def run_script(bridge: Bridge, path: str) -> int:
    failures = 0
    for raw in open(path):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("sleep "):
            time.sleep(float(line.split()[1]))
            continue
        verb, _, payload = line.partition(" ")
        ok, out = bridge.request(verb, payload)
        status = "ok " if ok else "ERR"
        print(f"[{status}] {line} -> {out}")
        if not ok:
            failures += 1
    return failures


def main(argv=None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    options = {"--wait": 0.0, "--timeout": 15.0}
    while args and args[0] in options:
        if len(args) < 2:
            args = []
            break
        try:
            options[args[0]] = float(args[1])
        except ValueError:
            print(f"{args[0]} needs a number", file=sys.stderr)
            return 2
        args = args[2:]
    if len(args) < 2 or (args[1] == "--script" and len(args) < 3):
        print(__doc__)
        print("usage: agentctl.py [--wait S] [--timeout S] TARGET <verb> [payload...]"
              " | TARGET --script <file>")
        return 2
    try:
        bridge = Bridge(args[0], timeout=options["--timeout"], wait=options["--wait"])
    except (OSError, EndpointError) as e:
        print(f"agentctl: cannot connect to {args[0]}: {e}", file=sys.stderr)
        return 3
    with bridge:
        if args[1] == "--script":
            return 1 if run_script(bridge, args[2]) else 0
        ok, out = bridge.request(args[1], " ".join(args[2:]))
    print(out)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
