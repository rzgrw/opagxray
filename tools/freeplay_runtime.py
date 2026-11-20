#!/usr/bin/env python3
"""Private CoC runtime for freeplay population AI tests.

  freeplay_runtime.py clone --from <runtime> --to <new-dir> --save <name>
  freeplay_runtime.py install --runtime <dir> [--force]

clone copies database/, gamedata/ and fsgame.ltx from a play runtime, gives the copy its own
appdata/ with the source user.ltx and the one named save, then installs the adapter. The
source runtime is only read. install copies res/coc/gamedata/scripts/xrsim_freeplay.script
into <dir>/gamedata/scripts/ and refuses to replace a different file unless --force is given.
"""
import argparse
import filecmp
from pathlib import Path
import shutil
import subprocess
import sys


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


def install(runtime: Path, force: bool):
    if not ADAPTER.is_file():
        raise ToolError(f'adapter source missing: {ADAPTER}')
    if not runtime.is_dir():
        raise ToolError(f'runtime is not a directory: {runtime}')
    target = runtime / ADAPTER_TARGET
    if target.is_symlink() or (target.exists() and not target.is_file()):
        raise ToolError(f'{target} is not a regular file')
    if target.exists():
        if filecmp.cmp(ADAPTER, target, shallow=False):
            print(f'adapter already installed: {target}')
            return
        if not force:
            raise ToolError(f'a different {target.name} is already installed at {target} (use --force)')
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ADAPTER, target)
    print(f'installed adapter: {target}')


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

    dest.mkdir(parents=True, exist_ok=True)
    for path in required:
        copy_tree(path, dest / path.name)
    shutil.copy2(source / 'fsgame.ltx', dest / 'fsgame.ltx')
    savedgames = dest / 'appdata' / 'savedgames'
    savedgames.mkdir(parents=True)
    shutil.copy2(source / 'appdata' / 'user.ltx', dest / 'appdata' / 'user.ltx')
    for path in saves:
        shutil.copy2(path, savedgames / path.name)
    print(f'cloned {source} -> {dest} with save {save}')
    install(dest, force=False)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    clone_parser = commands.add_parser('clone', help='make a private runtime copy with one save')
    clone_parser.add_argument('--from', dest='source', required=True, type=Path)
    clone_parser.add_argument('--to', dest='dest', required=True, type=Path)
    clone_parser.add_argument('--save', required=True)
    install_parser = commands.add_parser('install', help='install the CoC freeplay adapter')
    install_parser.add_argument('--runtime', required=True, type=Path)
    install_parser.add_argument('--force', action='store_true')
    args = parser.parse_args(argv)
    try:
        if args.command == 'clone':
            clone(args.source, args.dest, args.save)
        else:
            install(args.runtime, args.force)
    except (ToolError, OSError) as error:
        print(f'freeplay_runtime: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
