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
BRIDGE_FLAG = '-agent_bridge'
BRIDGE_DEFAULT_REL = 'appdata/agent_bridge.sock'
# C locale isspace, which is what sscanf("%s") consumes. Unicode spaces such as
# U+00A0 are not in this set and stay inside the token.
_C_LOCALE_SPACE = frozenset(' \t\n\v\f\r')
# proc_pidfdinfo(PROC_PIDFDSOCKETINFO) on this macOS returns 792 bytes, which is
# sizeof(socket_fdinfo): proc_fileinfo (24) plus socket_info padded to 768.
# soi_family is the int at 184, soi_kind is the int at 256, and un_sockinfo's
# sockaddr_un begins at 280 (sun_len, sun_family, sun_path[104]). SOCK_MAXADDRLEN
# is 255 and is only the address-union dummy, not the path field. soi_stat in
# that buffer is zero. Vnode flavors of the same fd return no bytes (EBADF), so
# the lstat inode is not available from these calls.
SOCKET_FDINFO_SIZE = 792
SOI_FAMILY_OFFSET = 184
SOI_KIND_OFFSET = 256
SUN_FAMILY_OFFSET = 281
BRIDGE_SUN_PATH_OFFSET = 282
BRIDGE_SUN_PATH_MAX = 104
AF_UNIX = 1
SOCKINFO_UN = 3
PROC_PIDFDSOCKETINFO = 3
PROX_FDTYPE_SOCKET = 2
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


def _engine_command_line(argv: list[str]) -> str:
    """Core.Params on macOS: argv[1:] joined, each element followed by a space."""
    if len(argv) <= 1:
        return ''
    return ''.join(item + ' ' for item in argv[1:])


def _agent_bridge_token(argv: list[str]) -> str | None:
    """Engine bind token, '' for the default socket, or None when the flag is absent.

    Mirrors strstr("-agent_bridge") and sscanf(" %s") in the C locale. Only
    space, tab, newline, vertical tab, form feed, and carriage return separate
    the token. A missing token or a token that starts with '-' selects
    appdata/agent_bridge.sock. No flag means the engine creates no socket.
    `-agent_bridge=` has no space, so the glued text is the token and is not
    the default.
    """
    command = _engine_command_line(argv)
    index = command.find(BRIDGE_FLAG)
    if index < 0:
        return None
    rest = command[index + len(BRIDGE_FLAG):]
    cursor = 0
    while cursor < len(rest) and rest[cursor] in _C_LOCALE_SPACE:
        cursor += 1
    if cursor >= len(rest):
        return ''
    end = cursor
    while end < len(rest) and rest[end] not in _C_LOCALE_SPACE:
        end += 1
    token = rest[cursor:end].replace('\\', '/')
    if token.startswith('-'):
        return ''
    return token


def _path_aliases(path: str) -> list[str]:
    """Lexical /var and /private/var spellings. Does not stat or follow a symlink."""
    found = [path]
    for prefix, swap in (('/private/var/', '/var/'), ('/var/', '/private/var/')):
        if path.startswith(prefix):
            found.append(swap + path[len(prefix):])
    unique = []
    for item in found:
        if item not in unique:
            unique.append(item)
    return unique


def _bridge_relative(root_real: Path, token: str) -> str:
    if token == '':
        return BRIDGE_DEFAULT_REL
    if token.startswith('/'):
        matches = []
        for root in _path_aliases(str(root_real)):
            prefix = root.rstrip('/') + '/'
            if token.startswith(prefix):
                matches.append(token[len(prefix):])
        if not matches:
            raise GateError('mismatch', 'unexpected_path', token, 'bridge socket leaves appdata')
        if any(item != matches[0] for item in matches):
            raise GateError('mismatch', 'ambiguous_path', token, 'bridge socket path is ambiguous')
        return matches[0]
    return token


def _bridge_bind_paths(root_real: Path, cwd: str, token: str, rel: str) -> list[str]:
    """Path strings bind() may have stored. Exact sun_path equality, not a suffix."""
    found: list[str] = []
    if token == '':
        for base in (cwd, str(root_real)):
            text = base.replace('\\', '/')
            if text.endswith('/'):
                found.append(text + BRIDGE_DEFAULT_REL)
            else:
                found.append(text + '/' + BRIDGE_DEFAULT_REL)
        found.append(BRIDGE_DEFAULT_REL)
    elif token.startswith('/'):
        found.append(token)
    else:
        found.append(token)
        found.append(str(root_real.joinpath(*rel.split('/'))))
    paths: list[str] = []
    for item in found:
        for alias in _path_aliases(item):
            if alias not in paths:
                paths.append(alias)
    return paths


def _lstat_bridge_socket(root_real: Path, rel: str) -> os.stat_result:
    parts = rel.split('/')
    cursor = root_real
    walked: list[str] = []
    for part in parts[:-1]:
        walked.append(part)
        cursor = cursor / part
        ancestor = '/'.join(walked)
        try:
            info = os.lstat(cursor)
        except FileNotFoundError as exc:
            raise GateError('mismatch', 'nonregular', rel,
                            'declared bridge endpoint is missing') from exc
        if stat.S_ISLNK(info.st_mode):
            raise GateError('mismatch', 'symlink', ancestor, 'symlink')
        if not stat.S_ISDIR(info.st_mode):
            raise GateError('mismatch', 'nonregular', ancestor,
                            'declared bridge endpoint is not a socket')
        if Path(os.path.realpath(cursor)) != cursor:
            raise GateError('mismatch', 'ambiguous_path', ancestor,
                            'canonical path differs from the lexical path')
    leaf = cursor / parts[-1]
    try:
        info = os.lstat(leaf)
    except FileNotFoundError as exc:
        raise GateError('mismatch', 'nonregular', rel, 'declared bridge endpoint is missing') from exc
    if stat.S_ISLNK(info.st_mode):
        raise GateError('mismatch', 'symlink', rel, 'symlink')
    if not stat.S_ISSOCK(info.st_mode):
        raise GateError('mismatch', 'nonregular', rel, 'declared bridge endpoint is not a socket')
    return info


def _libproc() -> ctypes.CDLL | None:
    try:
        library = ctypes.CDLL('/usr/lib/libproc.dylib', use_errno=True)
    except OSError:
        return None
    library.proc_pidinfo.argtypes = [
        ctypes.c_int, ctypes.c_int, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_int]
    library.proc_pidinfo.restype = ctypes.c_int
    library.proc_pidfdinfo.argtypes = [
        ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
    library.proc_pidfdinfo.restype = ctypes.c_int
    return library


def _pid_fd_bytes(library: ctypes.CDLL, pid: int) -> bytes | None:
    size = 256
    while size <= 1 << 20:
        ctypes.set_errno(0)
        buf = ctypes.create_string_buffer(size)
        got = library.proc_pidinfo(pid, 1, 0, buf, size)
        if got < 0:
            return None
        if got < size:
            return buf.raw[:got]
        size *= 2
    return None


def _unix_sun_path(data: bytes) -> bytes | None:
    """sun_path from one complete AF_UNIX socket_fdinfo, or None if unsupported.

    A short return, a non-UNIX family or kind, or a pathname whose NUL is
    outside sun_path[104] is rejected. This does not read soi_stat: on this
    Darwin that inode is zero and is not the directory node.
    """
    if len(data) != SOCKET_FDINFO_SIZE:
        return None
    family = int.from_bytes(data[SOI_FAMILY_OFFSET:SOI_FAMILY_OFFSET + 4], 'little', signed=True)
    kind = int.from_bytes(data[SOI_KIND_OFFSET:SOI_KIND_OFFSET + 4], 'little', signed=True)
    if family != AF_UNIX or kind != SOCKINFO_UN or data[SUN_FAMILY_OFFSET] != AF_UNIX:
        return None
    field = data[BRIDGE_SUN_PATH_OFFSET:BRIDGE_SUN_PATH_OFFSET + BRIDGE_SUN_PATH_MAX]
    end = field.find(b'\0')
    if end < 0:
        return None
    return field[:end]


def _process_owns_bridge_socket(pid: int, paths: list[str]) -> bool:
    """True when pid has an AF_UNIX fd whose sun_path is exactly one bind path.

    True means the C string matched. It does not mean that fd owns the inode
    now at that path.
    """
    wanted = []
    for path in paths:
        encoded = os.fsencode(path)
        if encoded and encoded not in wanted:
            wanted.append(encoded)
    if not wanted or pid <= 0:
        return False
    library = _libproc()
    if library is None:
        return False
    listing = _pid_fd_bytes(library, pid)
    if listing is None or len(listing) % 8:
        return False
    for offset in range(0, len(listing), 8):
        fd = int.from_bytes(listing[offset:offset + 4], 'little', signed=True)
        kind = int.from_bytes(listing[offset + 4:offset + 8], 'little')
        if kind != PROX_FDTYPE_SOCKET:
            continue
        ctypes.set_errno(0)
        raw = ctypes.create_string_buffer(4096)
        got = library.proc_pidfdinfo(pid, fd, PROC_PIDFDSOCKETINFO, raw, 4096)
        if got != SOCKET_FDINFO_SIZE:
            continue
        path = _unix_sun_path(raw.raw[:got])
        if path is not None and path in wanted:
            return True
    return False


def _directory_id(path: str) -> tuple[int, int] | None:
    """Device and inode of an existing directory. Final symlinks are followed.

    Identity is not a lowercased path. On a case-insensitive volume, two
    spellings of one directory share this id; on a case-sensitive volume they
    do not.
    """
    try:
        info = os.lstat(path)
    except OSError:
        return None
    if stat.S_ISLNK(info.st_mode):
        try:
            info = os.stat(path)
        except OSError:
            return None
    if not stat.S_ISDIR(info.st_mode):
        return None
    return (info.st_dev, info.st_ino)


def _ancestor_ids(path: str) -> list[tuple[int, int]]:
    current = os.path.normpath(path)
    found: list[tuple[int, int]] = []
    seen: set[str] = set()
    while current not in seen:
        seen.add(current)
        ident = _directory_id(current)
        if ident is not None and ident not in found:
            found.append(ident)
        parent = os.path.dirname(current)
        if parent == current:
            break
        current = parent
    return found


def _directories_overlap(left: str, right: str) -> bool:
    """True when two existing directories are equal or one contains the other.

    Comparison uses directory device/inode in both directions. realpath on
    this macOS volume keeps the requested spelling, so string prefixes miss
    a case alias of the same inode.
    """
    left_self = _directory_id(os.path.normpath(left))
    right_self = _directory_id(os.path.normpath(right))
    if left_self is None or right_self is None:
        return False
    return left_self in _ancestor_ids(right) or right_self in _ancestor_ids(left)


def _external_failure(path: str, detail: str) -> GateError:
    return GateError('mismatch', 'external_directory', path, detail)


def _sealed_external_directory(launcher: dict, passed: str | None) -> str | None:
    """Optional launcher field. A passed inspect value must match it exactly."""
    if 'external_communication_directory' not in launcher or launcher.get('external_communication_directory') is None:
        sealed = None
    else:
        sealed = launcher['external_communication_directory']
        if not isinstance(sealed, str) or not sealed:
            raise _external_failure(str(sealed), 'external communication directory is not a path')
    if passed is not None and passed != sealed:
        raise _external_failure(
            passed, 'inspect declaration does not match the sealed launcher record')
    return sealed


def _note_external_lstat(path: str, after_lstat) -> None:
    if after_lstat is not None:
        after_lstat(Path(path))
    deadline = _ACTIVE_DEADLINE.get()
    if deadline is not None:
        deadline.check()


def _lexical_absolute(text: str, detail: str) -> None:
    if not text.startswith('/') or text.endswith('/') or '\\' in text or '//' in text:
        raise _external_failure(text, detail)
    parts = text.split('/')
    if any(part in {'', '.', '..'} for part in parts[1:]):
        raise _external_failure(text, detail)
    if any(char.isspace() or char in _C_LOCALE_SPACE for char in text):
        raise _external_failure(text, detail)


def _generated_communication_roots(root_real: Path) -> list[tuple[str, str]]:
    """Alias and settings directories the external directory must not touch.

    Longer trees come first so appdata and configs win over their parents.
    """
    roots = [
        (root_real / 'appdata', 'settings closure'),
        (root_real / 'gamedata' / 'configs', 'settings closure'),
    ]
    for name in sorted(ARCHIVE_CONTAINERS):
        roots.append((root_real / 'database' / name, 'generated alias'))
    for name in sorted(GAMEDATA_CHILDREN):
        if name == 'configs':
            continue
        roots.append((root_real / 'gamedata' / name, 'generated alias'))
    roots.append((root_real / 'database', 'generated alias'))
    roots.append((root_real / 'gamedata', 'generated alias'))
    return [(os.path.realpath(path), kind) for path, kind in roots]


def _external_overlap(directory: str, root_real: Path, executable: str, manifest: dict) -> None:
    hits = []
    app_dir = os.path.dirname(os.path.realpath(executable))
    if _directories_overlap(directory, app_dir):
        hits.append('application directory')
    for key in ('base', 'patch', 'overlay'):
        root = os.path.realpath(manifest[key])
        if _directories_overlap(directory, root):
            hits.append('manifest root')
            break
    for path, kind in _generated_communication_roots(root_real):
        if _directories_overlap(directory, path):
            hits.append(kind)
            break
    runtime = os.path.realpath(root_real)
    if _directories_overlap(directory, runtime):
        hits.append('runtime')
    messages = {
        'settings closure': 'external communication directory overlaps a settings closure',
        'generated alias': 'external communication directory overlaps a generated alias',
        'manifest root': 'external communication directory overlaps a manifest root',
        'runtime': 'external communication directory overlaps the runtime',
        'application directory': 'external communication directory overlaps the application directory',
    }
    for kind in messages:
        if kind in hits:
            raise _external_failure(directory, messages[kind])


def _ordinary_external_socket(argv: list[str], directory: str) -> str:
    args = argv[1:]
    exact = [arg for arg in args if arg == BRIDGE_FLAG]
    embedded = [arg for arg in args if BRIDGE_FLAG in arg and arg != BRIDGE_FLAG]
    if len(exact) != 1 or embedded:
        raise _external_failure(directory, 'agent_bridge flag is not a single ordinary absolute path')
    index = args.index(BRIDGE_FLAG)
    if index + 1 >= len(args):
        raise _external_failure(directory, 'agent_bridge flag is not a single ordinary absolute path')
    token = args[index + 1]
    if token.startswith('-') or not token.startswith('/'):
        raise _external_failure(token, 'agent_bridge path is not the declared socket leaf')
    try:
        _lexical_absolute(token, 'agent_bridge path is not the declared socket leaf')
    except GateError as error:
        if any(char.isspace() or char in _C_LOCALE_SPACE for char in token):
            raise _external_failure(token, 'agent_bridge path contains whitespace') from error
        raise
    if len(os.fsencode(token)) > 103:
        raise _external_failure(token, 'agent_bridge path exceeds 103 bytes')
    prefix = directory + '/'
    leaf = token[len(prefix):] if token.startswith(prefix) else ''
    if not leaf or '/' in leaf or leaf.startswith('.'):
        raise _external_failure(token, 'agent_bridge path is not the declared socket leaf')
    return token


def _external_bridge_socket(root_real: Path, launcher: dict, directory: str, manifest: dict,
                            executable: str, after_lstat) -> dict:
    """One sealed external endpoint. lstat identity is change detection, not fd ownership.

    The runner is trusted only as a nonadversarial same-user process that created
    this directory. That assumption is not asset evidence and not proof the pid
    owns the socket inode. sun_path is not consulted.
    """
    _lexical_absolute(directory, 'external communication directory is not canonical')
    _note_external_lstat(directory, after_lstat)
    try:
        info = os.lstat(directory)
    except FileNotFoundError as exc:
        raise _external_failure(directory, 'external communication directory is missing') from exc
    if stat.S_ISLNK(info.st_mode):
        raise _external_failure(directory, 'external communication directory is a symlink')
    if not stat.S_ISDIR(info.st_mode):
        raise _external_failure(directory, 'external communication directory is not a directory')
    if os.path.realpath(directory) != directory:
        raise _external_failure(directory, 'external communication directory is not canonical')
    _external_overlap(directory, root_real, executable, manifest)
    if stat.S_IMODE(info.st_mode) != 0o700:
        raise _external_failure(directory, 'external communication directory mode is not 0700')
    if info.st_uid != os.geteuid():
        raise _external_failure(directory, 'external communication directory owner is not the current uid')
    token = _ordinary_external_socket(launcher['argv'], directory)
    # The callback may chmod or replace the directory before this lstat. The
    # lstat follows a directory symlink, so the directory is read again below
    # and that result wins over a deadline that expires in the same callback.
    if after_lstat is not None:
        after_lstat(Path(token))
    try:
        sock = os.lstat(token)
    except OSError as exc:
        sock_error: OSError | None = exc
        sock = None
    else:
        sock_error = None
    if after_lstat is not None:
        after_lstat(Path(directory))
    info = _recheck_external_directory(directory, info)
    deadline = _ACTIVE_DEADLINE.get()
    if deadline is not None:
        deadline.check()
    if sock is None:
        raise _external_failure(token, 'declared bridge endpoint is missing') from sock_error
    if stat.S_ISLNK(sock.st_mode):
        raise _external_failure(token, 'declared bridge endpoint is a symlink')
    if not stat.S_ISSOCK(sock.st_mode):
        raise _external_failure(token, 'declared bridge endpoint is not a socket')
    return {
        'path': token,
        'dev': str(sock.st_dev),
        'ino': str(sock.st_ino),
        'association': 'external-observation',
        'directory': {
            'path': directory,
            'dev': str(info.st_dev),
            'ino': str(info.st_ino),
            'mode': '0700',
            'uid': str(info.st_uid),
        },
    }


def _recheck_external_directory(directory: str, before: os.stat_result) -> os.stat_result:
    """Reject a directory that moved or changed mode while its socket was observed."""
    try:
        info = os.lstat(directory)
    except OSError as exc:
        raise GateError('unknown', 'stat_race', directory,
                        'external communication directory changed during inspection') from exc
    drifted = (
        stat.S_ISLNK(info.st_mode)
        or not stat.S_ISDIR(info.st_mode)
        or os.path.realpath(directory) != directory
        or info.st_dev != before.st_dev
        or info.st_ino != before.st_ino
        or stat.S_IMODE(info.st_mode) != stat.S_IMODE(before.st_mode)
        or info.st_uid != before.st_uid
    )
    if drifted:
        raise GateError('unknown', 'stat_race', directory,
                        'external communication directory changed during inspection')
    return info


def _bridge_socket(root_real: Path, launcher: dict, process, *, require_live_fd: bool,
                   external_directory: str | None = None, manifest: dict | None = None,
                   executable: str | None = None, after_lstat=None) -> dict | None:
    """The one communication endpoint, or None when the engine opens none."""
    declared = _sealed_external_directory(launcher, external_directory)
    if declared is not None:
        if manifest is None or executable is None:
            raise _external_failure(declared, 'external communication directory is not sealed with the runtime')
        return _external_bridge_socket(
            root_real, launcher, declared, manifest, executable, after_lstat)
    token = _agent_bridge_token(launcher['argv'])
    if token is None:
        return None
    rel = _bridge_relative(root_real, token)
    check_relative(rel)
    _dotfile(rel)
    if not rel.startswith('appdata/') or rel == 'appdata':
        raise GateError('mismatch', 'unexpected_path', token or rel, 'bridge socket leaves appdata')
    info = _lstat_bridge_socket(root_real, rel)
    observation = getattr(process, 'observation', 'live')
    association = 'launcher-record'
    # A dead pid has no fd table to read. Attest still compares the lstat identity.
    # Seal reaches this only after _observe has required the pid to be alive.
    if observation == 'live' and process.alive(launcher['pid']):
        if not _process_owns_bridge_socket(
                launcher['pid'], _bridge_bind_paths(root_real, launcher['cwd'], token, rel)):
            raise GateError('unknown', 'process_unverified', rel,
                            'bridge socket is not an AF_UNIX fd of the launched process')
        # Path text matched a fd. libproc does not identify which inode that fd
        # is, including after unlink and rebind, so this is not live-fd.
        association = 'path-only'
    elif observation == 'live' and require_live_fd:
        raise GateError('unknown', 'process_unverified', rel,
                        'bridge socket is not an AF_UNIX fd of the launched process')
    elif observation == 'live':
        association = 'not-rechecked'
    return {
        'path': rel,
        'dev': str(info.st_dev),
        'ino': str(info.st_ino),
        'association': association,
    }


def _bridge_identity(record: object) -> tuple[str, ...] | None:
    if record is None:
        return None
    if not isinstance(record, dict) or not isinstance(record.get('path'), str):
        return ('', '', '', '', '', '', '')
    directory = record.get('directory')
    if directory is None:
        directory_key = ('', '', '', '', '')
    elif not isinstance(directory, dict):
        directory_key = ('?', '?', '?', '?', '?')
    else:
        directory_key = (
            str(directory.get('path')), str(directory.get('dev')), str(directory.get('ino')),
            str(directory.get('mode')), str(directory.get('uid')),
        )
    return (record['path'], str(record.get('dev')), str(record.get('ino')), *directory_key)


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


def _rogue_archives(root_real: Path, manifest_keys: set[str], bridge_rel: str | None = None,
                    bridge_identity: tuple[str, str] | None = None) -> None:
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
            if bridge_rel is not None and rel == bridge_rel:
                try:
                    info = os.lstat(path)
                except FileNotFoundError as exc:
                    raise GateError('unknown', 'stat_race', rel,
                                    'bridge socket disappeared while scanning') from exc
                if not stat.S_ISSOCK(info.st_mode):
                    raise GateError('mismatch', 'nonregular', rel,
                                    'declared bridge endpoint is not a socket')
                if bridge_identity != (str(info.st_dev), str(info.st_ino)):
                    raise GateError('unknown', 'stat_race', rel, 'bridge socket changed while scanning')
                continue
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
                    sealed_images: list[dict] | None = None,
                    external_communication_directory: str | None = None) -> dict:
    """Rehash one runtime. This does not read a capture and does not accept a measurement.

    `external_communication_directory` defaults to None. Existing callers stay
    valid. When passed, it must equal the optional sealed launcher field.
    """
    token = _ACTIVE_DEADLINE.set(deadline)
    try:
        return _inspect_runtime(runtime, launcher_path, config=config, process=process,
                                deadline=deadline, chunk_size=chunk_size, after_lstat=after_lstat,
                                require_process=require_process, sealed_images=sealed_images,
                                external_communication_directory=external_communication_directory)
    finally:
        _ACTIVE_DEADLINE.reset(token)


def _inspect_runtime(runtime: Path, launcher_path: Path, *, config: str, process, deadline: Deadline,
                     chunk_size: int = CHUNK, after_lstat=None, require_process: bool = True,
                     sealed_images: list[dict] | None = None,
                     external_communication_directory: str | None = None) -> dict:
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
    bridge = _bridge_socket(
        root_real, launcher, process, require_live_fd=require_process,
        external_directory=external_communication_directory, manifest=manifest,
        executable=executable, after_lstat=after_lstat)
    if bridge is not None and bridge.get('association') == 'path-only':
        raise GateError('unknown', 'process_unverified', bridge['path'],
                        'AF_UNIX sun_path does not identify the current socket inode')
    _rogue_archives(root_real, keys, None if bridge is None else bridge['path'],
                    None if bridge is None else (bridge['dev'], bridge['ino']))
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
        'bridge_socket': bridge,
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
    if _bridge_identity(sealed.get('bridge_socket')) != _bridge_identity(current.get('bridge_socket')):
        shown = current.get('bridge_socket') or {}
        raise GateError('mismatch', 'launcher_identity',
                        shown.get('path', '') if isinstance(shown, dict) else '',
                        'bridge socket identity changed after the seal')
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
