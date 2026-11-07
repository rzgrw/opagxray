#!/usr/bin/env python3
"""Seal and attest a private runtime for one xray-frame-time-v1 capture.

Asset hashing does not import the frame-time reporter. The measurement gate is a
separate function and is the only place that parser is called. An overhead-probe
CSV is rejected before that call.
"""

from __future__ import annotations

import argparse
import contextvars
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[1]
if str(REPO) not in sys.path:
    sys.path.insert(0, str(REPO))

from tools.stage_coc_runtime import FORMAT as STAGE_FORMAT
from tools.stage_coc_runtime import filesystem_text

FORMAT = 'openxviberay-frame-time-provenance-v1'
TRUST = 'local-asset-attestation'
LAUNCHER_FORMAT = 'openxviberay-launcher-record-v1'
FRAME_SCHEMA = '# xray-frame-time-v1'
PROBE_SCHEMA = '# xray-overhead-probe-v1'
CHUNK = 1024 * 1024
ASSET_REASONS = frozenset({
    'unknown_provenance:content_manifest',
    'unknown_provenance:overlay',
})
SETTINGS_LIMIT = (
    'user.ltx bytes are not effective console state; '
    'SDL swap interval 0 is exported as off for both disabled and indeterminate; '
    'a negative frame limit stays unknown; '
    'resolution, vsync, and frame limit are marked unknown if they change while running'
)
ENGINE_SOURCES = (
    'FrameTimeCollector.h', 'FrameTimeCollector.cpp', 'FrameTimeSession.h',
    'FrameTimeSession.cpp', 'FrameTimeClassification.h', 'device.h', 'device.cpp',
    'x_ray.cpp', 'AgentBridge.cpp', 'CMakeLists.txt',
)
RENDER_SOURCES = (
    'Layers/xrRenderGL/glHW.cpp', 'Layers/xrRender/r__sync_point.cpp',
    'Layers/xrRender/QueryHelper.h',
)
ARCHIVE_CONTAINERS = {'config', 'maps', 'resource', 'sound', 'patch'}
GAMEDATA_CHILDREN = {
    'shaders', 'configs', 'ai', 'anims', 'levels', 'meshes', 'scripts', 'sounds',
    'spawns', 'textures',
}
EMPTY_TREES = GAMEDATA_CHILDREN - {'shaders', 'configs'}
UNBOUND = {
    'cdb_cache': 'appdata/cdb_cache',
    'savedgames': 'appdata/savedgames',
    'logs': 'appdata/logs',
    'screenshots': 'appdata/screenshots',
    'perf': 'appdata/perf',
}
NOT_EFFECTIVE_ROOTS = ('mp', 'levels', 'resources', 'localization', 'patches')
EXIT = {'verified': 0, 'mismatch': 1, 'unknown': 2, 'timeout': 3}
LC_ID_DYLIB = 0xD
LOAD_COMMANDS = {0xC, 0x80000018, 0x8000001F}
STAT_IDENTITY = ('st_dev', 'st_ino', 'st_size', 'st_mtime_ns', 'st_ctime_ns')
# Set for the duration of inspect_runtime so git and vmmap see the remaining budget
# without changing source_build's positional signature.
_ACTIVE_DEADLINE: contextvars.ContextVar = contextvars.ContextVar(
    'frame_time_provenance_deadline', default=None)
# A mapped region carries the address range as its own column, immediately
# before the bracketed size. A hyphen inside a path is not that column.
_VM_RANGE_TOKEN = re.compile(r'^[0-9a-fA-F]+-[0-9a-fA-F]+$')
_VM_REGION_LINE = re.compile(
    r'^(?P<kind>.+?)\s+(?P<range>[0-9a-fA-F]+-[0-9a-fA-F]+)\s+'
    r'\[(?P<size>[^\]]*)\]\s+(?P<prot>\S+)\s+(?P<mode>SM=\S+)'
    r'(?:\s+PURGE=\S+)?(?:\s+(?P<detail>.*))?$'
)


class GateError(Exception):
    def __init__(self, kind: str, code: str, path: str = '', detail: str = ''):
        self.kind = kind
        self.code = code
        self.path = path
        self.detail = detail
        super().__init__(detail or code)


class Deadline:
    def __init__(self, seconds: float, clock=time.monotonic):
        if seconds < 0:
            raise GateError('unknown', 'timeout_seconds', '', 'negative timeout')
        self.seconds = seconds
        self.clock = clock
        self.start = clock()

    def remaining(self) -> float:
        return self.seconds - (self.clock() - self.start)

    def check(self) -> None:
        if self.remaining() <= 0:
            raise GateError('timeout', 'timeout', '', 'deadline exceeded')


def unknown_token(value: object) -> bool:
    """Same token rule as the reporter. Not a probe-hash check."""
    return isinstance(value, str) and value.strip().lower() in {'unknown', 'unavailable'}


def aggregate(rows: list[tuple[str, str, int]]) -> str:
    blob = ''.join(f'{path}\0{sha}\0{size}\n' for path, sha, size in sorted(rows))
    return hashlib.sha256(blob.encode()).hexdigest()


def check_relative(rel: str) -> None:
    if not isinstance(rel, str) or not rel or rel.startswith('/'):
        raise GateError('mismatch', 'ambiguous_path', str(rel), 'not a relative path')
    if any(ord(ch) < 32 or ord(ch) == 127 for ch in rel):
        raise GateError('mismatch', 'ambiguous_path', rel, 'control character')
    if '\\' in rel or '//' in rel or rel.endswith(' ') or rel.endswith('.'):
        raise GateError('mismatch', 'ambiguous_path', rel, 'ambiguous path syntax')
    parts = rel.split('/')
    if any(part in {'', '.', '..'} for part in parts):
        raise GateError('mismatch', 'ambiguous_path', rel, 'empty, dot, or parent segment')


def is_engine_archive(name: str) -> bool:
    dot = name.rfind('.')
    if dot < 0:
        return False
    ext = name[dot:].lower()
    return ext.startswith('.db') or ext.startswith('.xdb')


def is_os_image(path: str) -> bool:
    real = os.path.realpath(path)
    return real == '/System' or real.startswith('/System/') or real == '/usr/lib' or real.startswith('/usr/lib/')


def _version(value: int) -> str:
    return f'{value >> 16}.{(value >> 8) & 0xff}.{value & 0xff}'


def parse_macho(data: bytes) -> dict | None:
    """Return identity and load names, or None when the bytes are not Mach-O 64."""
    if len(data) < 32 or int.from_bytes(data[:4], 'little') != 0xFEEDFACF:
        return None
    ncmds = int.from_bytes(data[16:20], 'little')
    sizeofcmds = int.from_bytes(data[20:24], 'little')
    if sizeofcmds < 0 or 32 + sizeofcmds > len(data):
        return None
    offset = 32
    end = 32 + sizeofcmds
    identity = None
    loads: list[str] = []
    for _ in range(ncmds):
        if offset + 8 > end:
            return None
        cmd = int.from_bytes(data[offset:offset + 4], 'little')
        cmdsize = int.from_bytes(data[offset + 4:offset + 8], 'little')
        if cmdsize < 8 or offset + cmdsize > end:
            return None
        if cmd == LC_ID_DYLIB or cmd in LOAD_COMMANDS:
            if cmdsize < 24:
                return None
            name_off = int.from_bytes(data[offset + 8:offset + 12], 'little')
            current = int.from_bytes(data[offset + 16:offset + 20], 'little')
            compat = int.from_bytes(data[offset + 20:offset + 24], 'little')
            if not 0 <= name_off < cmdsize:
                return None
            raw = data[offset + name_off:offset + cmdsize]
            name = raw.split(b'\0', 1)[0].decode('utf-8', 'surrogateescape')
            if cmd == LC_ID_DYLIB:
                identity = {'install_name': name, 'current_version': _version(current),
                            'compat_version': _version(compat)}
            else:
                loads.append(name)
        offset += cmdsize
    if offset != end:
        return None
    return {'identity': identity, 'loads': loads}


def _unparsed_file_region(line: str) -> bool:
    """A file-bearing region we could not read completely.

    Header lines such as ``Path: /tmp/a-b/xr_3da`` have no address-range
    column. A line that does, and also names an absolute path, is a mapping
    record; dropping it would omit that image.
    """
    tokens = line.split()
    if not any(_VM_RANGE_TOKEN.fullmatch(token) for token in tokens):
        return False
    return any(token.startswith('/') for token in tokens)


def images_from_vmmap_text(text: str, executable: str) -> list[str] | None:
    """Mapped-image paths from vmmap region lines.

    A region line has its hex address range as the column immediately before
    the bracketed size, then protection, ``SM=``, an optional ``PURGE=``, and
    the region detail. The detail is kept whole, so a path may contain spaces.
    ``Path: /tmp/a-b/xr_3da`` is a header: the hyphen is inside the path, not
    an address column. A file-bearing line that does not match this grammar
    refuses the whole observation instead of omitting that image. The
    executable must appear as one of the region paths.
    """
    found: list[str] = []
    seen: set[str] = set()
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        match = _VM_REGION_LINE.match(line)
        if match is None:
            if _unparsed_file_region(line):
                return None
            continue
        path = (match.group('detail') or '').strip()
        if not path:
            continue
        if not path.startswith('/'):
            continue
        if path in seen:
            continue
        seen.add(path)
        found.append(path)
    exe = os.path.realpath(executable)
    if not found or not any(os.path.realpath(path) == exe for path in found):
        return None
    return found


def _stat_identity(info: os.stat_result) -> tuple:
    return tuple(getattr(info, name) for name in STAT_IDENTITY)


def hash_regular(path: Path | str, deadline: Deadline, *, chunk_size: int = CHUNK,
                 after_lstat=None) -> tuple[str, int]:
    """Hash a regular file. Symlinks and other non-regular nodes fail closed.

    The deadline is checked before every streamed chunk, not only between files.
    A change between lstat and the read is unknown, not a content mismatch.
    """
    path = Path(path)
    try:
        st = os.lstat(path)
    except FileNotFoundError as exc:
        raise GateError('unknown', 'stat_race', str(path), 'path disappeared') from exc
    mode = st.st_mode
    if stat.S_ISLNK(mode):
        raise GateError('mismatch', 'symlink', str(path), 'symlink')
    if not stat.S_ISREG(mode):
        raise GateError('mismatch', 'nonregular', str(path), 'not a regular file')
    if after_lstat is not None:
        after_lstat(path)
    deadline.check()
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | getattr(os, 'O_CLOEXEC', 0))
    except FileNotFoundError as exc:
        raise GateError('unknown', 'stat_race', str(path), 'path disappeared') from exc
    except OSError as exc:
        raise GateError('unknown', 'stat_race', str(path), 'open failed after lstat') from exc
    try:
        fst = os.fstat(fd)
        if _stat_identity(fst) != _stat_identity(st):
            raise GateError('unknown', 'stat_race', str(path), 'stat changed before read')
        accepted = _stat_identity(fst)
        hasher = hashlib.sha256()
        read = 0
        while True:
            deadline.check()
            try:
                data = os.read(fd, chunk_size)
            except OSError as exc:
                raise GateError('unknown', 'unreadable', str(path), 'read failed') from exc
            if not data:
                break
            hasher.update(data)
            read += len(data)
        try:
            end = os.fstat(fd)
            now = os.lstat(path)
        except FileNotFoundError as exc:
            raise GateError('unknown', 'stat_race', str(path), 'path disappeared during read') from exc
        # Same-size in-place writes keep dev, ino, and size. mtime_ns and ctime_ns move.
        if (_stat_identity(end) != accepted or _stat_identity(now) != accepted
                or read != fst.st_size):
            raise GateError('unknown', 'stat_race', str(path), 'stat changed during read')
        deadline.check()
        return hasher.hexdigest(), read
    finally:
        os.close(fd)


def _list_dir(path: Path) -> list[os.DirEntry]:
    try:
        with os.scandir(path) as scan:
            entries = list(scan)
    except FileNotFoundError as exc:
        raise GateError('unknown', 'stat_race', str(path), 'directory disappeared') from exc
    try:
        with os.scandir(path) as scan:
            again = {entry.name for entry in scan}
    except FileNotFoundError as exc:
        raise GateError('unknown', 'stat_race', str(path), 'directory disappeared') from exc
    if {entry.name for entry in entries} != again:
        raise GateError('unknown', 'stat_race', str(path), 'directory changed while scanning')
    return entries


def _dotfile(rel: str) -> None:
    if any(part.startswith('.') for part in rel.split('/')):
        raise GateError('mismatch', 'unexpected_path', rel, 'dotfile')


def _classify(entry: os.DirEntry, rel: str) -> str:
    check_relative(rel)
    _dotfile(rel)
    if entry.is_symlink():
        raise GateError('mismatch', 'symlink', rel, 'symlink')
    if entry.is_dir(follow_symlinks=False):
        return 'dir'
    if entry.is_file(follow_symlinks=False):
        return 'file'
    raise GateError('mismatch', 'nonregular', rel, 'not a regular file or directory')


def _contained(root_real: Path, rel: str) -> Path:
    check_relative(rel)
    lexical = root_real.joinpath(*rel.split('/'))
    if lexical.is_symlink():
        raise GateError('mismatch', 'symlink', rel, 'symlink')
    real = Path(os.path.realpath(lexical))
    if real != lexical:
        raise GateError('mismatch', 'ambiguous_path', rel, 'canonical path differs from the lexical path')
    try:
        real.relative_to(root_real)
    except ValueError as exc:
        raise GateError('mismatch', 'unexpected_path', rel, 'path leaves the runtime') from exc
    return real


def prepare_root(runtime: Path) -> Path:
    runtime = Path(runtime)
    if runtime.is_symlink():
        raise GateError('mismatch', 'symlink', str(runtime), 'runtime root is a symlink')
    if not runtime.is_dir():
        raise GateError('unknown', 'runtime', str(runtime), 'runtime is not a directory')
    return Path(os.path.realpath(runtime))


def _load_json(path: Path, code: str) -> tuple[dict, bytes]:
    try:
        raw = path.read_bytes()
        doc = json.loads(raw)
    except (OSError, json.JSONDecodeError) as exc:
        raise GateError('unknown', code, str(path), 'unreadable json') from exc
    if not isinstance(doc, dict):
        raise GateError('unknown', code, str(path), 'json value is not an object')
    return doc, raw


def _manifest_record(rel: str, record: object) -> dict:
    if (not isinstance(record, dict) or not isinstance(record.get('source'), str)
            or not isinstance(record.get('sha256'), str)
            or not re.fullmatch(r'[0-9a-f]{64}', record['sha256'])
            or type(record.get('size')) is not int or record['size'] < 0):
        raise GateError('mismatch', 'manifest_record', rel, 'invalid manifest file record')
    return record


def load_launcher(path: Path, root_real: Path) -> tuple[dict, bytes]:
    doc, raw = _load_json(path, 'launcher')
    if doc.get('format') != LAUNCHER_FORMAT:
        raise GateError('unknown', 'launcher', str(path), 'unrecognized launcher record')
    pid = doc.get('pid')
    argv = doc.get('argv')
    executable = doc.get('executable')
    cwd = doc.get('cwd')
    if type(pid) is not int or pid <= 0 or not isinstance(executable, str) or not isinstance(cwd, str):
        raise GateError('unknown', 'launcher', str(path), 'launcher record fields are incomplete')
    if not isinstance(argv, list) or not all(isinstance(item, str) for item in argv):
        raise GateError('unknown', 'launcher', str(path), 'launcher argv is not a string list')
    if Path(os.path.realpath(cwd)) != root_real:
        raise GateError('mismatch', 'launcher_cwd', cwd, 'launcher cwd is not this runtime')
    return doc, raw


def _fsltx_argument(argv: list[str]) -> str | None:
    """Engine `-fsltx ` value. A space is required; `-fsltx=` is not a redirect."""
    for index, arg in enumerate(argv):
        if arg == '-fsltx':
            if index + 1 >= len(argv) or argv[index + 1] == '':
                raise GateError('mismatch', 'filesystem_redirect', '-fsltx', '-fsltx is missing its path')
            return argv[index + 1]
        marker = '-fsltx '
        if marker in arg:
            value = arg.split(marker, 1)[1].split(' ', 1)[0]
            if not value:
                raise GateError('mismatch', 'filesystem_redirect', arg, '-fsltx is missing its path')
            return value
    return None


def _overlay_argument(argv: list[str]) -> str | None:
    """Any `-overlaypath` form. The flag retargets appdata, so it fails closed."""
    for index, arg in enumerate(argv):
        if '-overlaypath' not in arg:
            continue
        if arg == '-overlaypath' and index + 1 < len(argv) and argv[index + 1] != '':
            return argv[index + 1]
        return arg
    return None


def _resolve_redirect(root_real: Path, spec: str) -> Path:
    candidate = Path(spec)
    if not candidate.is_absolute():
        candidate = root_real / spec
    return Path(os.path.realpath(candidate))


def _bind_filesystem(root_real: Path, argv: list[str]) -> str | None:
    """Accept only an explicit fsltx that is this runtime's generated file.

    Any overlaypath, and any fsltx that resolves elsewhere, is a mismatch.
    The foreign tree is not scanned. Byte equality with filesystem_text() is
    still required later, on the staged file.
    """
    fsltx = _fsltx_argument(argv)
    overlay = _overlay_argument(argv)
    if fsltx is not None:
        resolved = _resolve_redirect(root_real, fsltx)
        expected = Path(os.path.realpath(root_real / 'fsgame.ltx'))
        if resolved != expected:
            raise GateError('mismatch', 'filesystem_redirect', fsltx,
                            'fsltx is not the staged fsgame.ltx')
    if overlay is not None:
        raise GateError('mismatch', 'filesystem_redirect', overlay,
                        'overlaypath redirects appdata outside the generated layout')
    return fsltx


def console_spec(argv: list[str]) -> str:
    for index, arg in enumerate(argv):
        if arg == '-ltx' and index + 1 < len(argv):
            return argv[index + 1]
        if arg.startswith('-ltx '):
            return arg[5:].split(' ', 1)[0]
    return 'user.ltx'


def _cfg_name(spec: str) -> str:
    slash = max(spec.rfind('/'), spec.rfind('\\'))
    dot = spec.rfind('.')
    if dot > slash:
        spec = spec[:dot]
    return spec.replace('\\', '/') + '.ltx'


def resolve_cfg(root_real: Path, spec: str) -> tuple[Path, str] | None:
    name = _cfg_name(spec)
    if any(ord(ch) < 32 or ord(ch) == 127 for ch in name) or name.startswith('/'):
        raise GateError('mismatch', 'ambiguous_path', name, 'cfg_load path')
    candidates = [(root_real / 'appdata' / name, f'appdata/{name}'), (root_real / name, name)]
    for path, rel in candidates:
        check_relative(rel)
        if path.is_symlink():
            raise GateError('mismatch', 'symlink', rel, 'cfg_load target is a symlink')
        if path.is_file():
            _contained(root_real, rel)
            return path, rel
    literal = Path(name)
    if literal.is_absolute() and literal.exists():
        raise GateError('mismatch', 'unexpected_path', name, 'cfg_load target leaves the runtime')
    return None


def _strip_ini_comment(line: str) -> str:
    quote = False
    index = 0
    while index < len(line):
        char = line[index]
        if char == '"':
            quote = not quote
        elif not quote and (char == ';' or (char == '/' and index + 1 < len(line) and line[index + 1] == '/')):
            return line[:index]
        index += 1
    return line


def _include_names(text: str) -> list[str]:
    names = []
    for line in text.splitlines():
        stripped = _strip_ini_comment(line).strip()
        if not stripped.startswith('#') or '#include' not in stripped:
            continue
        parts = stripped.split('"')
        if len(parts) >= 2 and parts[1]:
            names.append(parts[1])
    return names


def _cfg_load_names(text: str) -> list[str]:
    names = []
    for line in text.splitlines():
        parts = _strip_ini_comment(line).split()
        if len(parts) >= 2 and parts[0] == 'cfg_load':
            names.append(parts[1])
    return names


def _read_text_matching(path: Path, sha: str, size: int) -> str:
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise GateError('unknown', 'unreadable', str(path), 'settings reread failed') from exc
    if hashlib.sha256(data).hexdigest() != sha or len(data) != size:
        raise GateError('unknown', 'stat_race', str(path), 'settings changed between hash and parse')
    return data.decode('utf-8', 'surrogateescape')


def _reject_archive(rel: str, name: str, manifest_keys: set[str]) -> None:
    if is_engine_archive(name) and rel not in manifest_keys:
        raise GateError('mismatch', 'unexpected_path', rel, 'alternate archive')


class FixtureProcess:
    """Explicit test double. observation is fixture, never a live attestation."""

    observation = 'fixture'

    def __init__(self, pid: int, executable: str, images: list[str] | None, alive: bool = True):
        self.pid = pid
        self.executable_path = executable
        self.image_paths = images
        self.is_alive = alive

    def alive(self, pid: int) -> bool:
        return self.is_alive and pid == self.pid

    def executable(self, pid: int) -> str | None:
        if not self.alive(pid):
            return None
        return self.executable_path

    def images(self, pid: int, deadline=None) -> list[str] | None:
        del deadline
        if not self.alive(pid) or self.image_paths is None:
            return None
        return list(self.image_paths)


class LiveProcess:
    observation = 'live'

    def alive(self, pid: int) -> bool:
        if pid <= 0:
            return False
        try:
            os.kill(pid, 0)
        except OSError:
            return False
        return True

    def executable(self, pid: int) -> str | None:
        if not self.alive(pid):
            return None
        try:
            library = ctypes.CDLL('/usr/lib/libproc.dylib', use_errno=True)
        except OSError:
            return None
        buffer = ctypes.create_string_buffer(4096)
        library.proc_pidpath.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_uint32]
        library.proc_pidpath.restype = ctypes.c_int
        if library.proc_pidpath(pid, buffer, 4096) <= 0:
            return None
        return buffer.value.decode()

    def images(self, pid: int, deadline=None) -> list[str] | None:
        executable = self.executable(pid)
        if not executable:
            return None
        active = deadline if deadline is not None else _ACTIVE_DEADLINE.get()
        timeout = 30.0
        if active is not None:
            active.check()
            timeout = active.remaining()
            if timeout <= 0:
                raise GateError('timeout', 'timeout', str(pid), 'deadline exceeded')
        try:
            proc = subprocess.run(['/usr/bin/vmmap', '-w', str(pid)], capture_output=True,
                                  text=True, timeout=timeout, check=False)
        except subprocess.TimeoutExpired as exc:
            raise GateError('timeout', 'timeout', str(pid), 'vmmap exceeded the remaining deadline') from exc
        except OSError:
            return None
        # A failed or empty vmmap is not a mapped-image observation. Headers are not records.
        if proc.returncode != 0 or not proc.stdout:
            return None
        return images_from_vmmap_text(proc.stdout, executable)


def _deadline_timeout() -> float | None:
    deadline = _ACTIVE_DEADLINE.get()
    if deadline is None:
        return None
    deadline.check()
    timeout = deadline.remaining()
    if timeout <= 0:
        raise GateError('timeout', 'timeout', '', 'deadline exceeded')
    return timeout


def source_build(config: str) -> str:
    """Regular telemetry CMake identity. This is not probe source-identity.json.

    The positional signature stays `(config)` so a one-argument replacement can
    still run. The active inspect deadline, when set, bounds git and each source read.
    """
    timeout = _deadline_timeout()
    try:
        head = subprocess.run(['git', 'rev-parse', '--verify', 'HEAD'], cwd=REPO, check=True,
                              capture_output=True, text=True, timeout=timeout).stdout.strip()
    except subprocess.TimeoutExpired as exc:
        raise GateError('timeout', 'timeout', '', 'git exceeded the remaining deadline') from exc
    except (OSError, subprocess.CalledProcessError) as exc:
        raise GateError('unknown', 'source_build', '', 'git HEAD unavailable') from exc
    if not re.fullmatch(r'[0-9a-f]+', head):
        raise GateError('unknown', 'source_build', '', 'git HEAD is not a commit id')
    identity = head
    for name in ENGINE_SOURCES:
        _deadline_timeout()
        path = REPO / 'src/xrEngine' / name
        try:
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
        except OSError as exc:
            raise GateError('unknown', 'source_build', str(path), 'source file unreadable') from exc
        identity += f'{name}:{digest};'
    for name in RENDER_SOURCES:
        _deadline_timeout()
        path = REPO / 'src' / name
        try:
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
        except OSError as exc:
            raise GateError('unknown', 'source_build', str(path), 'source file unreadable') from exc
        identity += f'{name}:{digest};'
    _deadline_timeout()
    telemetry = hashlib.sha256(identity.encode()).hexdigest()
    return f'git:{head},telemetry_sha256:{telemetry},config:{config}'


def _walk_configs(root_real: Path, manifest_keys: set[str], deadline: Deadline, chunk_size: int,
                  after_lstat) -> dict[str, tuple[str, int, str]]:
    texts: dict[str, tuple[str, int, str]] = {}

    def walk(directory: Path, rel_dir: str) -> None:
        if not directory.exists():
            return
        if directory.is_symlink():
            raise GateError('mismatch', 'symlink', rel_dir, 'symlink directory')
        for entry in _list_dir(directory):
            rel = f'{rel_dir}/{entry.name}' if rel_dir else entry.name
            kind = _classify(entry, rel)
            if kind == 'dir':
                walk(Path(entry.path), rel)
                continue
            _reject_archive(rel, entry.name, manifest_keys)
            _contained(root_real, rel)
            sha, size = hash_regular(entry.path, deadline, chunk_size=chunk_size, after_lstat=after_lstat)
            text = _read_text_matching(Path(entry.path), sha, size)
            texts[rel] = (sha, size, text)

    configs = root_real / 'gamedata/configs'
    if configs.exists():
        walk(configs, 'gamedata/configs')
    return texts


def _add_include(root_real: Path, current_rel: str, spec: str, texts: dict, manifest_keys: set[str],
                 deadline: Deadline, chunk_size: int, after_lstat, pending: list[str]) -> None:
    if any(ord(ch) < 32 or ord(ch) == 127 for ch in spec):
        raise GateError('mismatch', 'ambiguous_path', spec, 'include path')
    current = root_real.joinpath(*current_rel.split('/'))
    if '*' in spec:
        if '*.ltx' not in spec:
            raise GateError('unknown', 'include_glob', spec, 'only *.ltx includes are recognized')
        parent_rel = Path(current_rel).parent.as_posix()
        if parent_rel == '.':
            parent_rel = ''
        for entry in _list_dir(current.parent):
            rel = f'{parent_rel}/{entry.name}' if parent_rel else entry.name
            if entry.name.startswith('.'):
                raise GateError('mismatch', 'unexpected_path', rel, 'dotfile')
            if entry.is_symlink():
                raise GateError('mismatch', 'symlink', rel, 'symlink')
            if entry.is_file(follow_symlinks=False) and entry.name.endswith('.ltx') and rel not in texts:
                pending.append(rel)
        return
    spec_path = spec.replace('\\', '/')
    lexical = Path(os.path.normpath(current.parent / spec_path))
    try:
        rel_path = lexical.relative_to(root_real)
    except ValueError as exc:
        raise GateError('mismatch', 'unexpected_path', spec, 'include leaves the runtime') from exc
    rel = rel_path.as_posix()
    check_relative(rel)
    if not lexical.exists():
        return
    if lexical.is_symlink():
        raise GateError('mismatch', 'symlink', rel, 'include symlink')
    if not lexical.is_file():
        raise GateError('mismatch', 'nonregular', rel, 'include is not a regular file')
    _reject_archive(rel, lexical.name, manifest_keys)
    if rel not in texts:
        pending.append(rel)


def _settings(root_real: Path, argv: list[str], manifest_keys: set[str], deadline: Deadline,
              chunk_size: int, after_lstat) -> tuple[list[tuple[str, str, int]], list[str], str]:
    texts = _walk_configs(root_real, manifest_keys, deadline, chunk_size, after_lstat)
    spec = console_spec(argv)
    resolved = resolve_cfg(root_real, spec)
    if resolved is None:
        raise GateError('unknown', 'missing_settings', spec, 'console script was not found')
    path, rel = resolved
    _reject_archive(rel, path.name, manifest_keys)
    sha, size = hash_regular(path, deadline, chunk_size=chunk_size, after_lstat=after_lstat)
    console_text = _read_text_matching(path, sha, size)
    console_files = {rel: (sha, size, console_text)}
    pending_cfg = _cfg_load_names(console_text)
    seen_cfg = {rel}
    while pending_cfg:
        target = pending_cfg.pop(0)
        found = resolve_cfg(root_real, target)
        if found is None:
            raise GateError('unknown', 'missing_settings', target, 'cfg_load target was not found')
        target_path, target_rel = found
        if target_rel in seen_cfg:
            continue
        seen_cfg.add(target_rel)
        _reject_archive(target_rel, target_path.name, manifest_keys)
        target_sha, target_size = hash_regular(target_path, deadline, chunk_size=chunk_size,
                                                after_lstat=after_lstat)
        target_text = _read_text_matching(target_path, target_sha, target_size)
        console_files[target_rel] = (target_sha, target_size, target_text)
        pending_cfg.extend(_cfg_load_names(target_text))
    # Includes are parsed only for gamedata/configs files and the loose files they
    # pull in. The console script's own #include lines are not CInifile includes.
    queued = [item for item in texts if item not in console_files]
    seen_includes: set[str] = set()
    while queued:
        current = queued.pop(0)
        if current in console_files or current in seen_includes:
            continue
        seen_includes.add(current)
        if current not in texts:
            file_path = _contained(root_real, current)
            _reject_archive(current, file_path.name, manifest_keys)
            sha, size = hash_regular(file_path, deadline, chunk_size=chunk_size, after_lstat=after_lstat)
            texts[current] = (sha, size, _read_text_matching(file_path, sha, size))
        for spec_name in _include_names(texts[current][2]):
            _add_include(root_real, current, spec_name, texts, manifest_keys, deadline, chunk_size,
                         after_lstat, queued)
    rows = [(name, texts[name][0], texts[name][1]) for name in texts]
    rows.extend((name, console_files[name][0], console_files[name][1]) for name in console_files
                if name not in texts)
    paths = sorted({row[0] for row in rows})
    return rows, paths, rel


def _scan_assets(root_real: Path, manifest: dict, manifest_keys: set[str], deadline: Deadline,
                 chunk_size: int, after_lstat) -> list[tuple[str, str, int]]:
    disk: set[str] = set()
    rows: list[tuple[str, str, int]] = []
    database = root_real / 'database'
    if database.is_symlink():
        raise GateError('mismatch', 'symlink', 'database', 'symlink')
    if database.exists():
        for entry in _list_dir(database):
            rel = f'database/{entry.name}'
            kind = _classify(entry, rel)
            if kind == 'dir':
                if entry.name not in ARCHIVE_CONTAINERS:
                    raise GateError('mismatch', 'unexpected_path', rel, 'unexpected archive root')
                for child in _list_dir(Path(entry.path)):
                    child_rel = f'{rel}/{child.name}'
                    child_kind = _classify(child, child_rel)
                    if child_kind != 'file':
                        raise GateError('mismatch', 'unexpected_path', child_rel, 'nested archive entry')
                    if child_rel not in manifest_keys:
                        raise GateError('mismatch', 'unexpected_path', child_rel, 'file is not in the manifest')
                    disk.add(child_rel)
                continue
            if rel not in manifest_keys:
                raise GateError('mismatch', 'unexpected_path', rel, 'file is not in the manifest')
            disk.add(rel)
    gamedata = root_real / 'gamedata'
    if gamedata.exists():
        if gamedata.is_symlink() or not gamedata.is_dir():
            raise GateError('mismatch', 'symlink' if gamedata.is_symlink() else 'nonregular', 'gamedata', 'gamedata')
        for entry in _list_dir(gamedata):
            rel = f'gamedata/{entry.name}'
            kind = _classify(entry, rel)
            if kind != 'dir' or entry.name not in GAMEDATA_CHILDREN:
                raise GateError('mismatch', 'unexpected_path', rel, 'unexpected gamedata entry')
            if entry.name in EMPTY_TREES:
                nested = _list_dir(Path(entry.path))
                if nested:
                    sample = nested[0]
                    raise GateError('mismatch', 'unexpected_path', f'{rel}/{sample.name}', 'loose override')
                continue
            if entry.name == 'shaders':
                for dirpath, dirnames, filenames in os.walk(entry.path, followlinks=False):
                    parent = Path(dirpath)
                    rel_dir = parent.relative_to(root_real).as_posix()
                    if parent.is_symlink():
                        raise GateError('mismatch', 'symlink', rel_dir, 'shader symlink')
                    _dotfile(rel_dir)
                    holds_managed = rel_dir in {'gamedata/shaders', 'gamedata/shaders/gl'} or any(
                        key.startswith(rel_dir + '/') for key in manifest_keys)
                    if not holds_managed:
                        raise GateError('mismatch', 'unexpected_path', rel_dir,
                                        'shader directory is not a managed parent')
                    for dirname in list(dirnames):
                        directory = parent / dirname
                        child_rel = f'{rel_dir}/{dirname}'
                        if dirname.startswith('.'):
                            raise GateError('mismatch', 'unexpected_path', child_rel, 'dotfile')
                        if directory.is_symlink():
                            raise GateError('mismatch', 'symlink', child_rel, 'shader symlink')
                    for filename in filenames:
                        file_path = parent / filename
                        suffix = file_path.relative_to(root_real).as_posix()
                        kind_name = _classify_path(file_path, suffix)
                        if kind_name != 'file' or suffix not in manifest_keys:
                            raise GateError('mismatch', 'unexpected_path', suffix, 'shader file is not managed')
                        disk.add(suffix)
    for rel in sorted(manifest_keys):
        if rel == 'fsgame.ltx':
            continue
        if not (rel.startswith('database/') or rel.startswith('gamedata/shaders/')):
            raise GateError('mismatch', 'unexpected_path', rel, 'manifest key is outside managed roots')
        if rel not in disk:
            raise GateError('mismatch', 'membership', rel, 'manifest key is missing on disk')
        record = _manifest_record(rel, manifest['files'][rel])
        path = _contained(root_real, rel)
        sha, size = hash_regular(path, deadline, chunk_size=chunk_size, after_lstat=after_lstat)
        if sha != record['sha256'] or size != record['size']:
            raise GateError('mismatch', 'hash_mismatch', rel, 'staged bytes differ from the manifest')
        source = Path(record['source'])
        if source.is_symlink():
            raise GateError('mismatch', 'symlink', record['source'], 'source symlink')
        if not source.is_file():
            raise GateError('unknown', 'source_missing', record['source'], 'source file is missing')
        roots = [Path(manifest['base']), Path(manifest['patch']), Path(manifest['overlay'])]
        resolved = source.resolve()
        if not any(_is_under(resolved, item.resolve()) for item in roots):
            raise GateError('mismatch', 'unexpected_path', record['source'], 'source leaves base, patch, and overlay')
        source_sha, source_size = hash_regular(source, deadline, chunk_size=chunk_size, after_lstat=after_lstat)
        if source_sha != sha or source_size != size:
            raise GateError('mismatch', 'source_mismatch', rel, 'source bytes differ from the staged file')
        rows.append((rel, sha, size))
    fs_path = root_real / 'fsgame.ltx'
    if not fs_path.is_file() or fs_path.is_symlink():
        raise GateError('mismatch', 'membership', 'fsgame.ltx', 'generated fsgame.ltx is missing')
    if 'fsgame.ltx' not in manifest['files']:
        raise GateError('mismatch', 'membership', 'fsgame.ltx', 'manifest is missing fsgame.ltx')
    fs_record = _manifest_record('fsgame.ltx', manifest['files']['fsgame.ltx'])
    if fs_record['source'] != 'generated from res/fsgame.ltx':
        raise GateError('mismatch', 'fsgame_bytes', 'fsgame.ltx', 'fsgame source string is not the generator')
    sha, size = hash_regular(fs_path, deadline, chunk_size=chunk_size, after_lstat=after_lstat)
    data = fs_path.read_bytes()
    if hashlib.sha256(data).hexdigest() != sha or len(data) != size:
        raise GateError('unknown', 'stat_race', 'fsgame.ltx', 'fsgame changed during the read')
    if data != filesystem_text().encode():
        raise GateError('mismatch', 'fsgame_bytes', 'fsgame.ltx', 'fsgame bytes differ from filesystem_text()')
    if sha != fs_record['sha256'] or size != fs_record['size']:
        raise GateError('mismatch', 'hash_mismatch', 'fsgame.ltx', 'fsgame manifest record does not match')
    rows.append(('fsgame.ltx', sha, size))
    if set(manifest_keys) != {row[0] for row in rows}:
        raise GateError('mismatch', 'membership', '', 'managed membership differs from the manifest')
    return rows


def _classify_path(path: Path, rel: str) -> str:
    check_relative(rel)
    _dotfile(rel)
    if path.is_symlink():
        raise GateError('mismatch', 'symlink', rel, 'symlink')
    if path.is_dir():
        return 'dir'
    if path.is_file():
        return 'file'
    raise GateError('mismatch', 'nonregular', rel, 'not a regular file')


def _is_under(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
    except ValueError:
        return False
    return True


def _rogue_archives(root_real: Path, manifest_keys: set[str]) -> None:
    appdata = root_real / 'appdata'
    if not appdata.exists():
        return
    if appdata.is_symlink() or not appdata.is_dir():
        raise GateError('mismatch', 'symlink' if appdata.is_symlink() else 'nonregular', 'appdata', 'appdata')
    for dirpath, dirnames, filenames in os.walk(appdata, followlinks=False):
        parent = Path(dirpath)
        if parent.is_symlink():
            raise GateError('mismatch', 'symlink', 'appdata', 'appdata symlink')
        for name in list(dirnames):
            rel = (parent / name).relative_to(root_real).as_posix()
            check_relative(rel)
            _dotfile(rel)
            if (parent / name).is_symlink():
                raise GateError('mismatch', 'symlink', rel, 'symlink')
        for name in filenames:
            rel = (parent / name).relative_to(root_real).as_posix()
            check_relative(rel)
            _dotfile(rel)
            path = parent / name
            if path.is_symlink():
                raise GateError('mismatch', 'symlink', rel, 'symlink')
            if not path.is_file():
                raise GateError('mismatch', 'nonregular', rel, 'not a regular file')
            _reject_archive(rel, name, manifest_keys)


def _unbound(root_real: Path, deadline: Deadline, chunk_size: int, after_lstat) -> dict[str, str]:
    groups = {}
    for name, rel in UNBOUND.items():
        directory = root_real.joinpath(*rel.split('/'))
        rows: list[tuple[str, str, int]] = []
        if directory.exists():
            if directory.is_symlink() or not directory.is_dir():
                raise GateError('mismatch', 'symlink' if directory.is_symlink() else 'nonregular', rel, name)
            for dirpath, _dirs, filenames in os.walk(directory, followlinks=False):
                for filename in filenames:
                    path = Path(dirpath) / filename
                    file_rel = path.relative_to(root_real).as_posix()
                    if path.is_symlink() or not path.is_file():
                        continue
                    sha, size = hash_regular(path, deadline, chunk_size=chunk_size, after_lstat=after_lstat)
                    rows.append((file_rel, sha, size))
        groups[name] = aggregate(rows)
    return groups


def _observe(process, launcher: dict) -> tuple[str, list[str]]:
    pid = launcher['pid']
    if not process.alive(pid):
        raise GateError('unknown', 'process_unverified', str(pid), 'launched process is not alive')
    seen = process.executable(pid)
    images = process.images(pid)
    if not seen or images is None:
        raise GateError('unknown', 'process_unverified', str(pid), 'process images are unavailable')
    if os.path.realpath(seen) != os.path.realpath(launcher['executable']):
        raise GateError('mismatch', 'process_identity', seen, 'proc path differs from the launcher record')
    if not any(os.path.realpath(path) == os.path.realpath(seen) for path in images):
        raise GateError('unknown', 'process_unverified', seen, 'executable is not a mapped image')
    return seen, images


def _hash_images(executable: str, images: list[str], deadline: Deadline, chunk_size: int,
                 after_lstat) -> tuple[list[dict], list[str], list[str]]:
    hashed = []
    os_paths = []
    for image in images:
        if any(ord(ch) < 32 or ord(ch) == 127 for ch in image):
            raise GateError('mismatch', 'ambiguous_path', image, 'image path')
        if is_os_image(image):
            os_paths.append(os.path.realpath(image))
            continue
        path = Path(image)
        if path.is_symlink():
            raise GateError('mismatch', 'symlink', image, 'image symlink')
        if not path.is_file():
            raise GateError('unknown', 'process_unverified', image, 'mapped image is missing')
        sha, size = hash_regular(path, deadline, chunk_size=chunk_size, after_lstat=after_lstat)
        try:
            blob = path.read_bytes()
        except OSError as exc:
            raise GateError('unknown', 'unreadable', image, 'image reread failed') from exc
        if hashlib.sha256(blob).hexdigest() != sha or len(blob) != size:
            raise GateError('unknown', 'stat_race', image, 'image changed during parse')
        parsed = parse_macho(blob)
        if parsed is None:
            raise GateError('unknown', 'macho', image, 'mapped image is not parseable Mach-O')
        identity = parsed['identity'] or {}
        hashed.append({
            'path': os.path.realpath(image),
            'sha256': sha,
            'size': size,
            'current_version': identity.get('current_version'),
            'compat_version': identity.get('compat_version'),
            'install_name': identity.get('install_name'),
        })
    exe_real = os.path.realpath(executable)
    match = next((item for item in hashed if item['path'] == exe_real), None)
    if match is None:
        raise GateError('unknown', 'process_unverified', executable, 'executable was not hashed')
    try:
        blob = Path(exe_real).read_bytes()
    except OSError as exc:
        raise GateError('unknown', 'unreadable', exe_real, 'executable reread failed') from exc
    parsed = parse_macho(blob)
    if parsed is None:
        raise GateError('unknown', 'macho', exe_real, 'executable is not parseable Mach-O')
    return hashed, sorted(set(os_paths)), list(parsed['loads'])


def inspect_runtime(runtime: Path, launcher_path: Path, *, config: str, process, deadline: Deadline,
                    chunk_size: int = CHUNK, after_lstat=None, require_process: bool = True,
                    sealed_images: list[dict] | None = None) -> dict:
    """Rehash one runtime. This does not read a capture and does not accept a measurement."""
    token = _ACTIVE_DEADLINE.set(deadline)
    try:
        return _inspect_runtime(runtime, launcher_path, config=config, process=process,
                                deadline=deadline, chunk_size=chunk_size, after_lstat=after_lstat,
                                require_process=require_process, sealed_images=sealed_images)
    finally:
        _ACTIVE_DEADLINE.reset(token)


def _inspect_runtime(runtime: Path, launcher_path: Path, *, config: str, process, deadline: Deadline,
                     chunk_size: int = CHUNK, after_lstat=None, require_process: bool = True,
                     sealed_images: list[dict] | None = None) -> dict:
    root_real = prepare_root(Path(runtime))
    deadline.check()
    launcher, launcher_raw = load_launcher(Path(launcher_path), root_real)
    fsltx = _bind_filesystem(root_real, launcher['argv'])
    manifest_path = root_real / 'stage-manifest.json'
    manifest, manifest_raw = _load_json(manifest_path, 'manifest')
    if manifest.get('format') != STAGE_FORMAT or not isinstance(manifest.get('files'), dict):
        raise GateError('mismatch', 'manifest_format', 'stage-manifest.json', 'unrecognized staging manifest')
    for key in ('base', 'patch', 'overlay'):
        if not isinstance(manifest.get(key), str):
            raise GateError('mismatch', 'manifest_format', key, 'manifest root is missing')
    keys = set(manifest['files'])
    for rel in keys:
        check_relative(rel)
        _manifest_record(rel, manifest['files'][rel])
    if require_process:
        executable, images = _observe(process, launcher)
    else:
        executable = launcher['executable']
        images = [item['path'] for item in sealed_images or []]
        if process.alive(launcher['pid']):
            live_exe, live_images = _observe(process, launcher)
            live_set = {os.path.realpath(path) for path in live_images}
            sealed_set = {os.path.realpath(item['path']) for item in sealed_images or []}
            if os.path.realpath(live_exe) != os.path.realpath(executable) or live_set != sealed_set:
                raise GateError('mismatch', 'image_set', live_exe, 'mapped images changed while the process was alive')
            images = live_images
    _rogue_archives(root_real, keys)
    managed_rows = _scan_assets(root_real, manifest, keys, deadline, chunk_size, after_lstat)
    settings_rows, settings_paths, console_rel = _settings(
        root_real, launcher['argv'], keys, deadline, chunk_size, after_lstat)
    hashed, os_paths, loads = _hash_images(executable, images, deadline, chunk_size, after_lstat)
    # One positional argument. A replacement that only accepts config must still run.
    identity = source_build(config)
    deadline.check()
    unbound = _unbound(root_real, deadline, chunk_size, after_lstat)
    deadline.check()
    fsltx_path = None if fsltx is None else str(Path(os.path.realpath(root_real / 'fsgame.ltx')))
    return {
        'runtime': str(root_real),
        'manifest_sha256': hashlib.sha256(manifest_raw).hexdigest(),
        'manifest_format': manifest['format'],
        'base': manifest['base'],
        'patch': manifest['patch'],
        'overlay': manifest['overlay'],
        'managed_count': len(managed_rows),
        'managed_aggregate': aggregate(managed_rows),
        'managed_files': [{'path': path, 'sha256': sha, 'size': size} for path, sha, size in sorted(managed_rows)],
        'settings_aggregate': aggregate(settings_rows),
        'settings_paths': settings_paths,
        'console_script': console_rel,
        'images': hashed,
        'image_aggregate': aggregate((item['path'], item['sha256'], item['size']) for item in hashed),
        'os_images': os_paths,
        'configured_loads': loads,
        'source_build': identity,
        'config': config,
        'process_observation': getattr(process, 'observation', 'live'),
        'launcher_pid': launcher['pid'],
        'launcher_executable': os.path.realpath(executable),
        'launcher_argv': list(launcher['argv']),
        'launcher_cwd': os.path.realpath(launcher['cwd']),
        'launcher_sha256': hashlib.sha256(launcher_raw).hexdigest(),
        'fsltx': fsltx_path,
        'not_effective_roots': list(NOT_EFFECTIVE_ROOTS),
        'unbound': unbound,
        'settings_limit': SETTINGS_LIMIT,
    }


def _compare(sealed: dict, current: dict) -> list[str]:
    # Recorded launcher identity. Not a kernel process-start token: PID reuse after
    # exit, with this record left unchanged, stays an observation limit.
    if sealed.get('launcher_pid') != current['launcher_pid']:
        raise GateError('mismatch', 'launcher_identity', str(current['launcher_pid']),
                        'launcher pid changed after the seal')
    if sealed.get('launcher_executable') != current['launcher_executable']:
        raise GateError('mismatch', 'launcher_identity', str(current['launcher_executable']),
                        'launcher executable changed after the seal')
    if sealed.get('launcher_argv') != current['launcher_argv']:
        raise GateError('mismatch', 'launcher_identity', '',
                        'launcher argv changed after the seal')
    if sealed.get('launcher_cwd') != current['launcher_cwd']:
        raise GateError('mismatch', 'launcher_identity', str(current['launcher_cwd']),
                        'launcher cwd changed after the seal')
    if sealed.get('runtime') != current['runtime']:
        raise GateError('mismatch', 'launcher_identity', str(current['runtime']),
                        'runtime changed after the seal')
    if sealed.get('launcher_sha256') != current['launcher_sha256']:
        raise GateError('mismatch', 'launcher_identity', '',
                        'launcher record bytes changed after the seal')
    if sealed.get('manifest_sha256') != current['manifest_sha256']:
        raise GateError('mismatch', 'manifest_bytes', 'stage-manifest.json', 'manifest bytes changed')
    if sealed.get('managed_aggregate') != current['managed_aggregate']:
        sealed_files = {item['path']: item['sha256'] for item in sealed.get('managed_files', [])}
        current_files = {item['path']: item['sha256'] for item in current['managed_files']}
        for path in sorted(set(sealed_files) | set(current_files)):
            if sealed_files.get(path) != current_files.get(path):
                raise GateError('mismatch', 'hash_mismatch', path, 'managed bytes changed after the seal')
    if sealed.get('settings_aggregate') != current['settings_aggregate']:
        raise GateError('mismatch', 'settings', current['console_script'], 'settings bytes changed after the seal')
    if sealed.get('image_aggregate') != current['image_aggregate']:
        sealed_images = {item['path']: item['sha256'] for item in sealed.get('images', [])}
        for item in current['images']:
            if sealed_images.get(item['path']) != item['sha256']:
                raise GateError('mismatch', 'image_hash', item['path'], 'mapped image bytes changed')
        raise GateError('mismatch', 'image_set', '', 'mapped image set changed')
    if sealed.get('source_build') != current['source_build']:
        raise GateError('mismatch', 'source_build', '', 'recomputed source_build changed')
    changed = sorted(name for name, digest in current['unbound'].items()
                     if sealed.get('unbound', {}).get(name) != digest)
    return changed


def evaluate_frame_time_measurement(text: str) -> dict:
    """Frame-time campaign gate. Probe text never reaches the reporter."""
    first = text.splitlines()[0].rstrip('\r\n') if text else ''
    if first != FRAME_SCHEMA:
        return {
            'accepted': False,
            'reasons': ['unexpected_schema'],
            'reporter_invoked': False,
            'reporter_acceptance': None,
            'provenance': None,
            'asset_fields_unknown': False,
            'schema': first or 'empty',
        }
    from tools.frame_time_report import FLAG_WINDOW_VISIBILITY_UNKNOWN
    from tools.frame_time_report import CaptureError
    from tools.frame_time_report import _unknown
    from tools.frame_time_report import build_report
    from tools.frame_time_report import parse_capture
    meta: dict[str, str] = {}
    for line in text.splitlines()[1:]:
        if not line.startswith('#'):
            break
        if line.startswith('# ') and '=' in line[2:]:
            key, value = line[2:].split('=', 1)
            meta[key] = value
    fields_unknown = _unknown(meta.get('content_manifest', '')) and _unknown(meta.get('overlay', ''))
    try:
        capture = parse_capture(text)
    except CaptureError:
        return {
            'accepted': False,
            'reasons': ['schema'],
            'reporter_invoked': True,
            'reporter_acceptance': None,
            'provenance': {key: meta.get(key) for key in ('content_manifest', 'overlay', 'source_build', 'frame_limit')},
            'asset_fields_unknown': fields_unknown,
            'schema': FRAME_SCHEMA,
        }
    report = build_report(capture, scene='steady')
    reasons = list(report['reasons'])
    if any(int(row['flags']) & FLAG_WINDOW_VISIBILITY_UNKNOWN for row in capture.rows):
        reasons.append('window_visibility_unknown')
    leftover = [reason for reason in reasons if reason not in ASSET_REASONS]
    fields_unknown = _unknown(capture.metadata['content_manifest']) and _unknown(capture.metadata['overlay'])
    return {
        'accepted': not leftover and fields_unknown,
        'reasons': reasons,
        'reporter_invoked': True,
        'reporter_acceptance': report['acceptance'],
        'provenance': {field: capture.metadata[field] for field in (
            'content_manifest', 'overlay', 'source_build', 'renderer', 'frame_limit', 'vsync')},
        'asset_fields_unknown': fields_unknown,
        'schema': FRAME_SCHEMA,
    }


def _dump(doc: dict) -> bytes:
    return (json.dumps(doc, indent=2, sort_keys=True) + '\n').encode()


def _write_new(path: Path, payload: bytes) -> None:
    flags = os.O_CREAT | os.O_EXCL | os.O_WRONLY
    try:
        fd = os.open(path, flags, 0o644)
    except FileExistsError as exc:
        raise GateError('unknown', 'output_exists', str(path), 'refusing to overwrite') from exc
    try:
        view = payload
        while view:
            written = os.write(fd, view)
            view = view[written:]
    except Exception:
        os.close(fd)
        os.unlink(path)
        raise
    os.close(fd)


def _failure(command: str, error: GateError, extra: dict | None = None) -> dict:
    doc = {
        'format': FORMAT,
        'trust': TRUST,
        'command': command,
        'result': error.kind,
        'asset_verified': False,
        'files_matched': False,
        'measurement_accepted': False,
        'live_runtime_attested': False,
        'reporter_invoked': False,
        'failures': [{'code': error.code, 'path': error.path, 'detail': error.detail}],
    }
    if extra:
        doc.update(extra)
    return doc


def _finish(out: Path, command: str, func) -> int:
    if Path(out).exists():
        return EXIT['unknown']
    try:
        doc, code = func()
    except GateError as error:
        if error.code == 'output_exists':
            return EXIT['unknown']
        doc = _failure(command, error)
        code = EXIT[error.kind]
    try:
        _write_new(Path(out), _dump(doc))
    except GateError as error:
        if error.code == 'output_exists':
            return EXIT['unknown']
        raise
    return code


def seal(runtime, launcher_record, config, settle_seconds, timeout_seconds, out, *,
         process=None, clock=time.monotonic, chunk_size: int = CHUNK, after_lstat=None) -> int:
    if settle_seconds < 0:
        raise GateError('mismatch', 'settle_seconds', '', 'settle interval is negative')

    def run():
        deadline = Deadline(timeout_seconds, clock)
        current = inspect_runtime(runtime, launcher_record, config=config,
                                  process=process or LiveProcess(), deadline=deadline,
                                  chunk_size=chunk_size, after_lstat=after_lstat, require_process=True)
        verified = current['process_observation'] == 'live'
        doc = {
            'format': FORMAT, 'trust': TRUST, 'command': 'seal', 'result': 'verified',
            'asset_verified': True, 'files_matched': True, 'measurement_accepted': None,
            'live_runtime_attested': False, 'reporter_invoked': False,
            'settle_seconds': settle_seconds, 'failures': [], **current,
        }
        doc['live_runtime_attested'] = verified and doc['asset_verified']
        # A fixture observation is explicit and is not a live runtime attestation.
        if current['process_observation'] != 'live':
            doc['live_runtime_attested'] = False
        return doc, EXIT['verified']

    return _finish(Path(out), 'seal', run)


def _bind_capture(capture: Path, evidence: Path, deadline: Deadline, chunk_size: int) -> tuple[str, int, str, str]:
    capture_sha, capture_size = hash_regular(capture, deadline, chunk_size=chunk_size)
    evidence_sha, evidence_size = hash_regular(evidence, deadline, chunk_size=chunk_size)
    if capture_sha != evidence_sha or capture_size != evidence_size:
        raise GateError('mismatch', 'evidence', str(evidence), 'evidence bytes differ from the raw capture')
    try:
        text = Path(capture).read_text()
    except (OSError, UnicodeError) as exc:
        raise GateError('unknown', 'unreadable', str(capture), 'capture reread failed') from exc
    if hashlib.sha256(text.encode()).hexdigest() != capture_sha:
        raise GateError('unknown', 'stat_race', str(capture), 'capture changed after the hash')
    return capture_sha, capture_size, evidence_sha, text


def attest(runtime, launcher_record, seal_path, capture, evidence, timeout_seconds, out, *,
           process=None, clock=time.monotonic, chunk_size: int = CHUNK, after_lstat=None) -> int:
    def run():
        deadline = Deadline(timeout_seconds, clock)
        sealed, _raw = _load_json(Path(seal_path), 'seal_corrupt')
        if sealed.get('format') != FORMAT or sealed.get('command') != 'seal':
            raise GateError('unknown', 'seal_corrupt', str(seal_path), 'seal sidecar is not a provenance seal')
        if sealed.get('result') != 'verified' or sealed.get('asset_verified') is not True:
            raise GateError('unknown', 'seal_not_verified', str(seal_path), 'seal was not asset-verified')
        process_view = process or LiveProcess()
        if sealed.get('process_observation') != getattr(process_view, 'observation', 'live'):
            raise GateError('unknown', 'process_unverified', '', 'attest observation does not match the seal')
        sealed_paths = [dict(item) for item in (sealed.get('images') or [])]
        sealed_paths.extend({'path': path} for path in (sealed.get('os_images') or []))
        current = inspect_runtime(
            runtime, launcher_record, config=sealed.get('config', ''), process=process_view,
            deadline=deadline, chunk_size=chunk_size, after_lstat=after_lstat, require_process=False,
            sealed_images=sealed_paths)
        changed = _compare(sealed, current)
        try:
            capture_sha, capture_size, evidence_sha, text = _bind_capture(
                Path(capture), Path(evidence), deadline, chunk_size)
        except GateError as error:
            # The tree rehash already succeeded. Evidence is a separate binding.
            if error.code == 'evidence':
                doc = _failure('attest', error, extra={'files_matched': True, 'unbound_changes': changed})
                return doc, EXIT['mismatch']
            raise
        measurement = evaluate_frame_time_measurement(text)
        shared = {
            'capture_sha256': capture_sha, 'evidence_sha256': evidence_sha,
            'capture_size': capture_size, 'unbound_changes': changed,
            'process_observation': current['process_observation'],
            'managed_unchanged': True, 'settings_unchanged': True, 'manifest_unchanged': True,
            'fsgame_matches_generator': True,
        }
        if measurement['schema'] != FRAME_SCHEMA:
            doc = {
                'format': FORMAT, 'trust': TRUST, 'command': 'attest', 'result': 'unknown',
                'asset_verified': False, 'files_matched': True, 'measurement_accepted': False,
                'live_runtime_attested': False, 'reporter_invoked': False,
                'reasons': measurement['reasons'],
                'failures': [{'code': 'unexpected_schema', 'path': str(capture),
                              'detail': 'capture is not xray-frame-time-v1'}],
                **shared,
            }
            return doc, EXIT['unknown']
        if not measurement['asset_fields_unknown']:
            doc = _failure('attest', GateError(
                'mismatch', 'asset_field', str(capture),
                'content_manifest and overlay must stay unknown or unavailable'),
                extra={'files_matched': True, **shared, 'reasons': measurement['reasons'],
                       'reporter_invoked': measurement['reporter_invoked']})
            return doc, EXIT['mismatch']
        reported = (measurement.get('provenance') or {}).get('source_build')
        if reported is not None and reported != current['source_build']:
            doc = _failure('attest', GateError(
                'mismatch', 'source_build', str(capture),
                'CSV source_build differs from the recomputed string'),
                extra={'files_matched': True, **shared, 'reasons': measurement['reasons'],
                       'reporter_invoked': measurement['reporter_invoked']})
            return doc, EXIT['mismatch']
        measurement_accepted = bool(measurement['accepted'] and measurement['reporter_invoked'])
        if measurement['reasons'] == ['schema']:
            measurement_accepted = False
        doc = {
            'format': FORMAT, 'trust': TRUST, 'command': 'attest', 'result': 'verified',
            'asset_verified': True, 'files_matched': True,
            'measurement_accepted': measurement_accepted,
            'live_runtime_attested': current['process_observation'] == 'live',
            'reporter_invoked': measurement['reporter_invoked'],
            'reporter_acceptance': measurement['reporter_acceptance'],
            'reasons': measurement['reasons'],
            'provenance': measurement['provenance'],
            'seal_sha256': hashlib.sha256(Path(seal_path).read_bytes()).hexdigest(),
            'managed_aggregate': current['managed_aggregate'],
            'settings_aggregate': current['settings_aggregate'],
            'source_build': current['source_build'],
            'failures': [],
            'settings_limit': SETTINGS_LIMIT,
            **shared,
        }
        # After comparison, capture binding, report evaluation, and the seal
        # reread above. A budget that expires in that tail is not a success.
        deadline.check()
        return doc, EXIT['verified']

    return _finish(Path(out), 'attest', run)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    seal_parser = sub.add_parser('seal')
    seal_parser.add_argument('--runtime', type=Path, required=True)
    seal_parser.add_argument('--launcher-record', type=Path, required=True)
    seal_parser.add_argument('--config', required=True)
    seal_parser.add_argument('--settle-seconds', type=float, required=True)
    seal_parser.add_argument('--timeout-seconds', type=float, default=180)
    seal_parser.add_argument('--out', type=Path, required=True)
    attest_parser = sub.add_parser('attest')
    attest_parser.add_argument('--runtime', type=Path, required=True)
    attest_parser.add_argument('--launcher-record', type=Path, required=True)
    attest_parser.add_argument('--seal', type=Path, required=True)
    attest_parser.add_argument('--capture', type=Path, required=True)
    attest_parser.add_argument('--evidence', type=Path, required=True)
    attest_parser.add_argument('--timeout-seconds', type=float, default=180)
    attest_parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == 'seal':
            return seal(args.runtime, args.launcher_record, args.config, args.settle_seconds,
                        args.timeout_seconds, args.out)
        return attest(args.runtime, args.launcher_record, args.seal, args.capture, args.evidence,
                      args.timeout_seconds, args.out)
    except GateError as error:
        print(f'frame-time provenance: {error.code}: {error.detail}', file=sys.stderr)
        return EXIT.get(error.kind, 2)


if __name__ == '__main__':
    raise SystemExit(main())
