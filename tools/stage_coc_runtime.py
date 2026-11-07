#!/usr/bin/env python3
"""Stage a private OpenXVibeRay GL runtime from an installed CoC base + patch.

Only database archives are imported. Scripts/config defaults stay in the original
archives; runtime-written config overrides and appdata belong to this runtime.
Stop the engine before staging. Inputs must remain unchanged during staging.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import tempfile

REPOSITORY = Path(__file__).resolve().parents[1]
MANIFEST = 'stage-manifest.json'
FORMAT = 'openxviberay-coc-runtime-v1'


def digest(path: Path) -> str:
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def overlaps(left: Path, right: Path) -> bool:
    return left == right or left in right.parents or right in left.parents


def check_destination(output: Path) -> None:
    # Inspect lexical ancestors before resolving: resolving first hides escapes.
    for path in (output, *output.parents):
        if path.is_symlink():
            raise ValueError(f'Destination symlink is not allowed: {path}')
    if output.exists():
        if not output.is_dir():
            raise ValueError(f'Output is not a directory: {output}')
        for path in output.rglob('*'):
            if path.is_symlink():
                raise ValueError(f'Destination symlink is not allowed: {path}')
            if path.is_file() and path.stat().st_nlink > 1:
                raise ValueError(f'Destination hard link is not private: {path}')


def archives(root: Path) -> dict[Path, Path]:
    database = root / 'database'
    return {path.relative_to(root): path for path in sorted(database.rglob('*'))
            if path.is_file() and re.fullmatch(r'\.db[0-9]*', path.suffix.lower())}


def filesystem_text() -> str:
    # Preserve target-specific aliases; replace only the archive layout/appdata.
    template = (REPOSITORY / 'res/fsgame.ltx').read_text()
    lines = []
    for line in template.splitlines():
        key = line.split('=', 1)[0].strip()
        if key == '$app_data_root$':
            lines.append('$app_data_root$         = true| false| $fs_root$| appdata\\')
        elif key.startswith('$arch_dir') or key == '$game_arch_mp$':
            continue
        else:
            lines.append(line)
    archive_lines = ['$arch_dir$              = false| false| $fs_root$| database\\']
    for directory in ('config', 'maps', 'resource', 'sound', 'patch'):
        alias = 'patches' if directory == 'patch' else directory
        archive_lines.append(f'$arch_dir_{alias}$ = false| false| $arch_dir$| {directory}\\')
    return '\n'.join(archive_lines + lines) + '\n'


def atomic_write(destination: Path, *, source: Path | None = None, data: bytes | None = None) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix='.stage-', dir=destination.parent)
    os.close(fd)
    temporary = Path(name)
    try:
        if source is not None:
            shutil.copyfile(source, temporary)
        else:
            temporary.write_bytes(data)
        temporary.chmod(0o644)
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


def stage(base: Path, patch: Path, output: Path) -> dict:
    base, patch = base.resolve(), patch.resolve()
    # abspath keeps symlinks visible while eliminating lexical '..' components.
    output = Path(os.path.abspath(output))
    shader_root = REPOSITORY / 'res/gamedata/shaders/gl'
    check_destination(output)
    for source in (base, patch, shader_root):
        if overlaps(output.resolve(), source.resolve()):
            raise ValueError(f'Output/source overlap: {output} and {source}')
    base_files, patch_files = archives(base), archives(patch)
    for directory in ('config', 'maps', 'resource', 'sound'):
        if not any(p.parts[:2] == ('database', directory) for p in base_files):
            raise ValueError(f'Base is missing database/{directory} archives: {base}')
    for name in ('scripts', 'configs'):
        if Path(f'database/config/{name}.db') not in patch_files:
            raise ValueError(f'Patch is missing database/config/{name}.db: {patch}')
    sources = {**base_files, **patch_files}
    sources.update({Path('gamedata/shaders/gl') / p.relative_to(shader_root): p
                    for p in sorted(shader_root.rglob('*')) if p.is_file()})
    if not shader_root.is_dir():
        raise ValueError(f'Target GL shader directory is missing: {shader_root}')
    for source in sources.values():
        if source.is_symlink() or not any(source.resolve().is_relative_to(root)
                                         for root in (base, patch, shader_root)):
            raise ValueError(f'Source escapes asset roots: {source}')

    prior = {}
    manifest_path = output / MANIFEST
    if output.exists() and any(output.iterdir()):
        if not manifest_path.is_file():
            raise ValueError('Nonempty output is not a managed OpenXVibeRay runtime')
        prior = json.loads(manifest_path.read_text())
        if not isinstance(prior, dict) or prior.get('format') != FORMAT:
            raise ValueError('Output has an unrecognized staging manifest')
        records = prior.get('files')
        if not isinstance(records, dict):
            raise ValueError('Staging manifest files must be an object')
        for name, record in records.items():
            if (not isinstance(record, dict)
                    or not isinstance(record.get('source'), str)
                    or not isinstance(record.get('sha256'), str)
                    or not re.fullmatch(r'[0-9a-f]{64}', record['sha256'])
                    or type(record.get('size')) is not int or record['size'] < 0):
                raise ValueError(f'Invalid staging manifest file record: {name}')
    old_files = prior.get('files', {})
    files = {}
    pending = []
    for relative, source in sources.items():
        checksum = digest(source)
        files[relative.as_posix()] = {'source': str(source), 'sha256': checksum,
                                     'size': source.stat().st_size}
        pending.append((relative, source, None, checksum))
    fs_bytes = filesystem_text().encode()
    fs_hash = hashlib.sha256(fs_bytes).hexdigest()
    files['fsgame.ltx'] = {'source': 'generated from res/fsgame.ltx', 'sha256': fs_hash,
                           'size': len(fs_bytes)}
    pending.append((Path('fsgame.ltx'), None, fs_bytes, fs_hash))

    # Validate every collision before creating directories or writing any file.
    changes = []
    for relative, source, data, checksum in pending:
        destination = output / relative
        for parent in destination.parents:
            if parent == output:
                break
            if parent.exists() and not parent.is_dir():
                raise ValueError(f'Destination parent is not a directory: {parent}')
        if destination.exists():
            if not destination.is_file():
                raise ValueError(f'Destination is not a file: {destination}')
            existing_hash = digest(destination)
            old_hash = old_files.get(relative.as_posix(), {}).get('sha256')
            if old_hash is None or existing_hash not in (old_hash, checksum):
                raise ValueError(f'Refusing to overwrite unowned or modified file: {destination}')
            if existing_hash == checksum:
                continue
        changes.append((destination, source, data))
    # Refuse obsolete managed assets instead of silently loading stale archives.
    obsolete = set(old_files) - set(files)
    if obsolete:
        raise ValueError(f'Managed asset set changed; choose a fresh output: {sorted(obsolete)}')
    for relative in ('appdata', 'gamedata/configs'):
        path = output / relative
        if path.exists() and not path.is_dir():
            raise ValueError(f'Private writable directory is not a directory: {path}')
    settings = output / 'appdata/user.ltx'
    if settings.exists() and not settings.is_file():
        raise ValueError(f'User settings are not a file: {settings}')

    for destination, source, data in changes:
        atomic_write(destination, source=source, data=data)
    (output / 'gamedata/configs').mkdir(parents=True, exist_ok=True)
    (output / 'appdata').mkdir(parents=True, exist_ok=True)
    if not settings.exists():
        atomic_write(settings, data=b'renderer renderer_r3\nvid_mode 1280x720\nrs_fullscreen off\nrs_v_sync on\nsnd_volume_eff 0.\nsnd_volume_music 0.\n')
    manifest = {'format': FORMAT, 'base': str(base), 'patch': str(patch),
                'overlay': str(shader_root), 'files': files}
    atomic_write(manifest_path, data=(json.dumps(manifest, indent=2, sort_keys=True) + '\n').encode())
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', required=True, type=Path)
    parser.add_argument('--patch', required=True, type=Path)
    parser.add_argument('--output', type=Path, default=REPOSITORY / 'build/runtime/coc')
    args = parser.parse_args()
    try:
        manifest = stage(args.base, args.patch, args.output)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f'Private runtime: {args.output.resolve()} ({len(manifest["files"])} managed files)')
    print('Provenance: stage-manifest.json; writable files: appdata/ and gamedata/configs/')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
