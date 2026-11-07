#!/usr/bin/env python3
"""Generate reviewable source identity; no binary identity is invented here."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

BASE = 'e3f273f04bf85b4b265004cbbc8a35f19796cd31'
TARGET = '45680801007e8c5a3fab7555eb4e4ed17a35ffe3'
CORE = ['src/xrEngine/OverheadProbe.h', 'src/xrEngine/OverheadProbe.cpp']
CALLSITES = ['src/xrEngine/x_ray.cpp', 'src/xrEngine/AgentBridge.cpp',
             'src/xrEngine/FrameTimeSession.h', 'src/xrEngine/FrameTimeSession.cpp',
             'src/xrEngine/CMakeLists.txt']
ADDED = CORE + ['src/xrEngine/OverheadExperiment.h', 'src/xrEngine/OverheadExperiment.cpp',
                'cmake/OverheadExperiment.cmake', 'tools/prepare_overhead_identity.py']

def prepare(source, out, role):
    def git(*args):
        return subprocess.check_output(['git', '-C', str(source), *args])
    revision = git('rev-parse', 'HEAD').decode().strip()
    if revision != (BASE if role == 'base' else TARGET):
        raise ValueError('unpinned source revision')
    patch = git('diff', '--no-ext-diff', '--binary', 'HEAD', '--', *CALLSITES)
    core = b''.join((source / p).read_bytes() for p in CORE)
    names = set(git('ls-files').decode().splitlines()) | set(ADDED)
    # Source snapshot includes every tracked source file, including experimental
    # additions, excluding directories (submodules are recorded separately).
    entries = {p: hashlib.sha256((source / p).read_bytes()).hexdigest()
               for p in sorted(names) if (source / p).is_file()}
    snapshot = hashlib.sha256(''.join(f'{p}\0{h}\n' for p,h in entries.items()).encode()).hexdigest()
    submodules = git('submodule', 'status', '--recursive').decode()
    if any(line[0] != ' ' for line in submodules.splitlines()):
        raise ValueError('uninitialized or unpinned submodules')
    dirty_submodules = git('submodule', 'foreach', '--quiet', '--recursive', 'git status --porcelain --untracked-files=no').decode()
    if dirty_submodules.strip():
        raise ValueError('modified pinned dependency source')
    values = dict(ROLE=role, REVISION=revision, CORE=hashlib.sha256(core).hexdigest(),
                  CALLSITE=hashlib.sha256(patch).hexdigest(), SOURCE=snapshot)
    out.mkdir(parents=True, exist_ok=True)
    (out/'OverheadIdentity.h').write_text('\n'.join(f'#define XR_OVERHEAD_{k} "{v}"' for k,v in values.items()) +
        f'\n#define XR_OVERHEAD_HAS_COLLECTOR {int(role != "base")}\n#define XR_OVERHEAD_CAPTURE {int(role == "on")}\n')
    (out/'callsite.patch').write_bytes(patch)
    (out/'source-identity.json').write_text(json.dumps(dict(values=values, files=entries, submodules=submodules), indent=2)+'\n')
    return values

if __name__ == '__main__':
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--role', choices=['base','off','on'],required=True);a=p.parse_args();prepare(a.source,a.out,a.role)
