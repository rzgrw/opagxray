#!/usr/bin/env python3
"""Private CoC runtime for freeplay population AI tests.

  freeplay_runtime.py clone --from <runtime> --to <new-dir> --save <name>
  freeplay_runtime.py install --runtime <dir> [--fix-story-themes] [--force]

clone copies database/, gamedata/ and fsgame.ltx from a play runtime, gives the copy its own
appdata/ with the source user.ltx (game volumes forced to 0) and the one named save, then installs the adapter
and the story-theme fix. The source runtime is only read. install copies
res/coc/gamedata/scripts/xrsim_freeplay.script into <dir>/gamedata/scripts/ and refuses to replace a different
file unless --force is given; with --fix-story-themes it also writes the story-theme fix.

Story-theme fix (a known stock CoC 1.4.22 bug): sound_theme.script's npc_sound defaults a theme without a
`shuffle` key to "rand", which select_next_sound does not know, so a campfire story aborts the game at
sound_theme.script:206. The fix is generated from the runtime's own database/config/configs.db, so no CoC
content lives in this repo. Rule: every configs/misc/sound/*.ltx file in that archive is read (script_sound.ltx and
the files it includes, which is what the script loads); a section is patched when its name starts with
`strange_story_`, its `type` is `npc` and it has no `shuffle` key (both resolved through parents defined in the
same file). `shuffle = rnd` is inserted on the line after the section header, with the header's line ending; every
other byte is kept. Each file with at least one patched section is written to <runtime>/gamedata/<same path>,
where the loose file overrides the packed one ($game_config$ is scanned after the archives). An existing file
there that differs from the generated one is refused unless --force is given; an identical one is left alone.
"""
import argparse
import filecmp
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zlib

try:
    from tools import xrdb_unpack
except ImportError:   # run as a script from tools/
    import xrdb_unpack


REPO = Path(__file__).resolve().parents[1]
ADAPTER = REPO / 'res' / 'coc' / 'gamedata' / 'scripts' / 'xrsim_freeplay.script'
ADAPTER_TARGET = Path('gamedata') / 'scripts' / 'xrsim_freeplay.script'
ASSET_DIRS = ('database', 'gamedata')


class ToolError(Exception):
    pass


def is_within(path: Path, root: Path) -> bool:
    return path == root or root in path.parents


def copy_tree(source: Path, dest: Path):
    """APFS clone when available (instant), plain copy otherwise."""
    if sys.platform == 'darwin':
        result = subprocess.run(['cp', '-c', '-R', str(source), str(dest)], capture_output=True, text=True)
        if result.returncode == 0:
            return
        if dest.exists():
            shutil.rmtree(dest)
    shutil.copytree(source, dest, symlinks=True)


SILENT_SETTINGS = (('snd_volume_eff', 'snd_volume_eff 0.'), ('snd_volume_music', 'snd_volume_music 0.'))


def silenced_user_ltx(text: str) -> str:
    """The source settings with both game volumes forced to 0 (replaced in place or appended).

    Private runtimes run with -agent_sound, which opens a real audio device; they must stay silent."""
    lines = text.splitlines()
    for key, setting in SILENT_SETTINGS:
        found = False
        for i, line in enumerate(lines):
            if line.split(None, 1)[:1] == [key]:
                lines[i] = setting
                found = True
        if not found:
            lines.append(setting)
    return '\n'.join(lines) + '\n'


CONFIGS_DB = Path('database') / 'config' / 'configs.db'
SOUND_CONFIGS = 'configs/misc/sound/'
STORY_PREFIX = 'strange_story_'
SHUFFLE_LINE = b'shuffle = rnd'


def parse_sections(lines):
    """[(name, header line index, parents, {key: value})] of an LTX file split into byte lines."""
    sections = []
    for index, line in enumerate(lines):
        text = line.split(b';', 1)[0].strip()
        if text.startswith(b'['):
            match = re.match(rb'\[([^\]]+)\]\s*(?::(.*))?$', text)
            if not match:
                continue
            parents = [p.strip() for p in (match.group(2) or b'').split(b',') if p.strip()]
            sections.append((match.group(1).strip(), index, parents, {}))
        elif sections and b'=' in text:
            key, value = text.split(b'=', 1)
            sections[-1][3][key.strip()] = value.strip()
    return sections


def patch_story_themes(data: bytes, prefix: str = STORY_PREFIX):
    """(patched bytes, names of patched sections); see the module docstring for the rule."""
    lines = data.splitlines(keepends=True)
    sections = parse_sections(lines)
    by_name = {name: (parents, keys) for name, _, parents, keys in sections}

    def lookup(name, key, seen=()):
        if name not in by_name or name in seen:
            return None
        parents, keys = by_name[name]
        if key in keys:
            return keys[key]
        for parent in reversed(parents):   # later parents override earlier ones
            value = lookup(parent, key, seen + (name,))
            if value is not None:
                return value
        return None

    eol = b'\r\n' if b'\r\n' in data else b'\n'
    patched = []
    for name, index, _, _ in reversed(sections):
        if not name.startswith(prefix.encode()) or lookup(name, b'type') != b'npc':
            continue
        if lookup(name, b'shuffle') is not None:
            continue
        header = lines[index]
        body = header.rstrip(b'\r\n')
        ending = header[len(body):] or eol
        lines[index:index + 1] = [body + ending, SHUFFLE_LINE + ending]
        patched.append(name.decode('cp1251'))
    return b''.join(lines), list(reversed(patched))


def story_theme_fixes(runtime: Path):
    """{relative target path: patched bytes} generated from <runtime>/database/config/configs.db."""
    archive = runtime / CONFIGS_DB
    if not archive.is_file():
        raise ToolError(f'archive missing: {archive}')
    data = archive.read_bytes()
    try:
        _, entries = xrdb_unpack.read_table(data)
    except Exception as error:   # malformed archive: report, do not crash
        raise ToolError(f'cannot read {archive}: {error}')
    fixes = {}
    for name, real, compressed, crc, offset in entries:
        rel = name.replace('\\', '/')
        if not (rel.lower().startswith(SOUND_CONFIGS) and rel.lower().endswith('.ltx')) or '..' in rel.split('/'):
            continue
        raw = data[offset:offset + compressed]
        body = raw if real == compressed else xrdb_unpack.lzo1x_decompress(raw, real)
        if (zlib.crc32(body) & 0xffffffff) != crc:
            raise ToolError(f'crc mismatch for {rel} in {archive}')
        fixed, sections = patch_story_themes(body)
        if sections:
            fixes[Path('gamedata') / rel] = (fixed, sections)
    return fixes


def check_story_targets(runtime: Path, fixes, force: bool):
    """Refuse before anything is written when a differing file is in the way."""
    for rel, (fixed, _) in fixes.items():
        target = runtime / rel
        if target.is_symlink() or (target.exists() and not target.is_file()):
            raise ToolError(f'{target} is not a regular file')
        if target.exists() and target.read_bytes() != fixed and not force:
            raise ToolError(f'a different {target.name} is already at {target} (use --force)')


def write_story_fixes(runtime: Path, fixes):
    for rel, (fixed, sections) in fixes.items():
        target = runtime / rel
        if target.exists() and target.read_bytes() == fixed:
            print(f'story-theme fix already installed: {target}')
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(fixed)
        print(f'story-theme fix: {target} (shuffle = rnd in {len(sections)} sections)')
    if not fixes:
        print('story-theme fix: no section needs it')


def install(runtime: Path, force: bool, fix_story_themes: bool = False):
    if not ADAPTER.is_file():
        raise ToolError(f'adapter source missing: {ADAPTER}')
    if not runtime.is_dir():
        raise ToolError(f'runtime is not a directory: {runtime}')
    fixes = story_theme_fixes(runtime) if fix_story_themes else {}
    check_story_targets(runtime, fixes, force)
    target = runtime / ADAPTER_TARGET
    if target.is_symlink() or (target.exists() and not target.is_file()):
        raise ToolError(f'{target} is not a regular file')
    if target.exists() and filecmp.cmp(ADAPTER, target, shallow=False):
        print(f'adapter already installed: {target}')
    elif target.exists() and not force:
        raise ToolError(f'a different {target.name} is already installed at {target} (use --force)')
    else:
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ADAPTER, target)
        print(f'installed adapter: {target}')
    if fix_story_themes:
        write_story_fixes(runtime, fixes)


def clone(source: Path, dest: Path, save: str):
    source = source.resolve()
    dest = dest.resolve()
    if not source.is_dir():
        raise ToolError(f'source runtime is not a directory: {source}')
    if is_within(dest, source):
        raise ToolError(f'destination {dest} is inside the source runtime {source}')
    if dest.exists() and (not dest.is_dir() or any(dest.iterdir())):
        raise ToolError(f'destination {dest} exists and is not an empty directory')
    if not save or '/' in save or '\\' in save or save.startswith('.'):
        raise ToolError(f'bad save name: {save!r}')

    required = [source / name for name in ASSET_DIRS]
    files = [source / 'fsgame.ltx', source / 'appdata' / 'user.ltx']
    saves = [source / 'appdata' / 'savedgames' / f'{save}.{ext}' for ext in ('scop', 'scoc')]
    for path in required:
        if not path.is_dir():
            raise ToolError(f'source directory missing: {path}')
    for path in files + saves:
        if not path.is_file():
            raise ToolError(f'source file missing: {path}')
    if not ADAPTER.is_file():
        raise ToolError(f'adapter source missing: {ADAPTER}')
    inherited = source / ADAPTER_TARGET
    if inherited.exists() and not (inherited.is_file() and filecmp.cmp(ADAPTER, inherited, shallow=False)):
        raise ToolError(f'source runtime already carries a different adapter: {inherited}')
    check_story_targets(source, story_theme_fixes(source), force=False)

    dest.mkdir(parents=True, exist_ok=True)
    for path in required:
        copy_tree(path, dest / path.name)
    shutil.copy2(source / 'fsgame.ltx', dest / 'fsgame.ltx')
    savedgames = dest / 'appdata' / 'savedgames'
    savedgames.mkdir(parents=True)
    settings = (source / 'appdata' / 'user.ltx').read_text(encoding='latin-1')
    (dest / 'appdata' / 'user.ltx').write_text(silenced_user_ltx(settings), encoding='latin-1')
    for path in saves:
        shutil.copy2(path, savedgames / path.name)
    print(f'cloned {source} -> {dest} with save {save}')
    install(dest, force=False, fix_story_themes=True)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    clone_parser = commands.add_parser('clone', help='make a private runtime copy with one save')
    clone_parser.add_argument('--from', dest='source', required=True, type=Path)
    clone_parser.add_argument('--to', dest='dest', required=True, type=Path)
    clone_parser.add_argument('--save', required=True)
    install_parser = commands.add_parser('install', help='install the CoC freeplay adapter')
    install_parser.add_argument('--runtime', required=True, type=Path)
    install_parser.add_argument('--fix-story-themes', action='store_true',
                                help='also write the story-theme shuffle fix generated from configs.db')
    install_parser.add_argument('--force', action='store_true', help='replace differing files')
    args = parser.parse_args(argv)
    try:
        if args.command == 'clone':
            clone(args.source, args.dest, args.save)
        else:
            install(args.runtime, args.force, args.fix_story_themes)
    except (ToolError, OSError) as error:
        print(f'freeplay_runtime: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
