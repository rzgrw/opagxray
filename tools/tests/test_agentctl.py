import contextlib
import io
import socket
import tempfile
import threading
import unittest
from pathlib import Path

from tools import agentctl


class FakeBridge:
    """Minimal protocol-v1 TCP server on 127.0.0.1 (one client, echoes verbs)."""

    def __init__(self):
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.port = self.listener.getsockname()[1]
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    def _serve(self):
        try:
            conn, _ = self.listener.accept()
        except OSError:
            return
        with conn, conn.makefile("rb") as reader:
            for raw in reader:
                rid, _, rest = raw.decode().rstrip("\n").partition(" ")
                verb, _, payload = rest.partition(" ")
                if verb == "state":
                    reply = f"{rid} ok scene=menu fps=60\n"
                elif verb == "multi":
                    reply = f"{rid} ok line1\\nline2\n"
                elif verb == "bye":
                    conn.sendall(f"{rid} ok\n".encode())
                    return
                else:
                    reply = f"{rid} err unknown verb: {verb}\n"
                conn.sendall(f"999 ok stale\n{reply}".encode())  # stale id must be skipped

    def close(self):
        self.listener.close()
        self.thread.join(2)


class ResolveEndpointTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_tcp_spec(self):
        self.assertEqual(agentctl.resolve_endpoint("tcp:127.0.0.1:47000"), ("tcp", ("127.0.0.1", 47000)))
        self.assertEqual(agentctl.resolve_endpoint("tcp:[::1]:5"), ("tcp", ("::1", 5)))
        for bad in ["tcp:127.0.0.1", "tcp::80", "tcp:h:0", "tcp:h:65536", "tcp:h:-1", "tcp:h:x"]:
            with self.subTest(bad=bad), self.assertRaises(agentctl.EndpointError):
                agentctl.resolve_endpoint(bad)

    def test_port_file_and_appdata_dir(self):
        port_file = self.dir / "agent_bridge.port"
        port_file.write_text("51234\n", encoding="ascii")
        self.assertEqual(agentctl.resolve_endpoint(str(port_file)), ("tcp", ("127.0.0.1", 51234)))
        self.assertEqual(agentctl.resolve_endpoint(str(self.dir)), ("tcp", ("127.0.0.1", 51234)))

    def test_bad_port_files(self):
        port_file = self.dir / "agent_bridge.port"
        for content in ["", "0\n", "70000\n", "abc\n", " \n"]:
            port_file.write_text(content, encoding="ascii")
            with self.subTest(content=content), self.assertRaises(agentctl.EndpointError):
                agentctl.resolve_endpoint(str(port_file))
        with self.assertRaises(agentctl.EndpointError):
            agentctl.resolve_endpoint(str(self.dir / "missing.port"))

    def test_empty_dir_is_an_error(self):
        with self.assertRaises(agentctl.EndpointError):
            agentctl.resolve_endpoint(str(self.dir))

    def test_other_paths_stay_unix_socket_paths(self):
        path = str(self.dir / "agent_bridge.sock")
        self.assertEqual(agentctl.resolve_endpoint(path), ("unix", path))


class TcpRoundTripTests(unittest.TestCase):
    def setUp(self):
        self.server = FakeBridge()

    def tearDown(self):
        self.server.close()

    def test_bridge_request_over_tcp(self):
        with agentctl.Bridge(f"tcp:127.0.0.1:{self.server.port}", timeout=5) as bridge:
            self.assertEqual(bridge.request("state"), (True, "scene=menu fps=60"))
            self.assertEqual(bridge.request("multi"), (True, "line1\nline2"))
            self.assertEqual(bridge.request("nope"), (False, "unknown verb: nope"))
            self.assertEqual(bridge.request("bye"), (True, ""))

    def test_cli_with_appdata_dir(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "agent_bridge.port").write_text(f"{self.server.port}\n", encoding="ascii")
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                rc = agentctl.main(["--timeout", "5", tmp, "state"])
            self.assertEqual(rc, 0)
            self.assertEqual(out.getvalue(), "scene=menu fps=60\n")

    def test_wait_gives_up_with_error_code(self):
        with tempfile.TemporaryDirectory() as tmp:
            err = io.StringIO()
            with contextlib.redirect_stderr(err):
                rc = agentctl.main(["--wait", "0.3", str(Path(tmp) / "agent_bridge.port"), "state"])
            self.assertEqual(rc, 3)
            self.assertIn("port file not found", err.getvalue())


if __name__ == "__main__":
    unittest.main()
