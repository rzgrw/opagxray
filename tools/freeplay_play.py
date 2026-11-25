#!/usr/bin/env python3
"""Play CoC with the freeplay population AI on your Claude subscription: one command.

  python3 tools/freeplay_play.py [--runtime DIR] [--mode live|recorded] [--model NAME]
                                 [--calls-per-minute N] [--cli PATH] [--background] [--dry-run]

Defaults: runtime build/runtime/play, mode live, model sonnet, 6 calls per minute.

Checks, in order, and stops with one plain sentence (exit 1) before anything is written or launched:
the engine binary this repo builds exists; the runtime is a staged game folder (fsgame.ltx, database/);
for live mode, Claude Code is found (--cli, else XRAY_AGENT_CLI, else `claude` on PATH) and
`claude auth status` says it is signed in (that command makes no model call; the account details it prints
are never shown). Then it installs into the runtime the freeplay adapter (always brought up to date with this
repo's copy) and the story-theme fix (a differing override already there is refused, not overwritten), both
through tools/freeplay_runtime.py, and replaces itself with the game, started from inside the runtime with
`-agent_bridge appdata/agent_bridge.sock -xrsim_freeplay <mode>`. For live mode the game's environment gets
XRAY_AGENT_PROVIDER=claude_cli, XRAY_AGENT_MODEL, XRAY_FREEPLAY_CALLS_PER_MINUTE and XRAY_AGENT_CLI (the
CLI found above); API key variables are removed so the subscription is what gets used, and so are the
variables that tie a process to a running Claude Code session. --background adds
`-agent_background -agent_sound` for automated runs. --dry-run prints the checks, the files it would
install, and the command with its environment changes, and changes nothing.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import freeplay_runtime  # noqa: E402

REPO = freeplay_runtime.REPO
DEFAULT_RUNTIME = REPO / 'build' / 'runtime' / 'play'
DEFAULT_MODEL = 'sonnet'
DEFAULT_CALLS_PER_MINUTE = 6
BRIDGE = 'appdata/agent_bridge.sock'
KEY_VARIABLES = ('ANTHROPIC_API_KEY', 'XRAY_AGENT_API_KEY', 'ANTHROPIC_AUTH_TOKEN')
SESSION_VARIABLES = ('CLAUDECODE', 'CLAUDE_PID', 'CLAUDE_EFFORT', 'CLAUDE_AGENT_SDK_VERSION', 'CLAUDE_CODE_SESSION_ID',
                     'CLAUDE_CODE_MESSAGING_SOCKET', 'CLAUDE_CODE_MESSAGING_TOKEN', 'CLAUDE_CODE_CHILD_SESSION',
                     'CLAUDE_CODE_EMIT_SESSION_STATE_EVENTS', 'CLAUDE_CODE_ENTRYPOINT', 'CLAUDE_CODE_SESSION_ATTENDED',
                     'CLAUDE_CODE_ARTIFACT', 'CLAUDE_CODE_EXECPATH')
TOKEN_CHARS = set('ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._/@:+-')


class Stop(Exception):
    """A preflight failure: one plain sentence for the player."""


def engine_binary(system=None, machine=None) -> Path:
    system = system or platform.system()
    machine = (machine or platform.machine()).lower()
    if system not in ('Darwin', 'Linux'):
        raise Stop('This launcher supports macOS and Linux only for now.')
    arch = 'arm64' if machine in ('arm64', 'aarch64') else 'x64'
    return REPO / 'bin' / arch / 'Release' / 'xr_3da'


def is_token(value: str) -> bool:
    return bool(value) and value[0] != '-' and set(value) <= TOKEN_CHARS


def find_cli(cli_arg, environ, which):
    name = cli_arg or environ.get('XRAY_AGENT_CLI') or 'claude'
    if not is_token(name):
        raise Stop(f'The Claude Code path "{name}" is not usable: it must be one word.')
    if '/' in name:
        path = Path(name)
        if not (path.is_absolute() and path.is_file() and os.access(path, os.X_OK)):
            raise Stop(f'Claude Code was not found at {name}. Install it, sign in by running `claude`, then try again.')
        return str(path)
    found = which(name)
    if not found:
        raise Stop('Claude Code was not found. Install it, sign in by running `claude`, then try again.')
    return found


def check_signed_in(cli: str, env, run):
    try:
        result = run([cli, 'auth', 'status', '--json'], capture_output=True, text=True, timeout=60, env=env)
    except (OSError, subprocess.SubprocessError):
        raise Stop('Claude Code did not answer `claude auth status`. Run `claude` once in a terminal and try again.')
    try:
        status = json.loads(result.stdout or '')
    except ValueError:
        status = None
    if not isinstance(status, dict) or 'loggedIn' not in status:
        raise Stop('Claude Code did not report whether it is signed in. Run `claude auth login` and try again.')
    if status.get('loggedIn') is not True:
        raise Stop('Claude Code is not signed in. Run `claude auth login` and try again.')


def game_environment(environ, mode, model, calls, cli):
    env = {k: v for k, v in environ.items() if k not in KEY_VARIABLES and k not in SESSION_VARIABLES}
    added = {}
    if mode == 'live':
        added = {'XRAY_AGENT_PROVIDER': 'claude_cli', 'XRAY_AGENT_MODEL': model,
                 'XRAY_FREEPLAY_CALLS_PER_MINUTE': str(calls), 'XRAY_AGENT_CLI': cli}
        env.update(added)
    removed = sorted(k for k in environ if k in KEY_VARIABLES or k in SESSION_VARIABLES)
    return env, added, removed


def plan_install(runtime: Path):
    """What installing would do, checked before anything is written: [(path, action)] and the story fixes."""
    fixes = freeplay_runtime.story_theme_fixes(runtime)
    freeplay_runtime.check_story_targets(runtime, fixes, force=False)
    target = runtime / freeplay_runtime.ADAPTER_TARGET
    if target.is_symlink() or (target.exists() and not target.is_file()):
        raise freeplay_runtime.ToolError(f'{target} is not a regular file')
    same = target.exists() and target.read_bytes() == freeplay_runtime.ADAPTER.read_bytes()
    actions = [(target, 'up to date' if same else ('update' if target.exists() else 'install'))]
    for rel, (fixed, _) in fixes.items():
        path = runtime / rel
        actions.append((path, 'up to date' if path.exists() and path.read_bytes() == fixed else 'install'))
    return actions, fixes


def install(runtime: Path, fixes):
    # The adapter is this project's own file: always brought up to date (force). Story fixes were checked.
    freeplay_runtime.install(runtime, force=True, fix_story_themes=False)
    freeplay_runtime.write_story_fixes(runtime, fixes)


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--runtime', type=Path, default=DEFAULT_RUNTIME)
    parser.add_argument('--mode', choices=('live', 'recorded'), default='live')
    parser.add_argument('--model', default=DEFAULT_MODEL)
    parser.add_argument('--calls-per-minute', type=int, default=DEFAULT_CALLS_PER_MINUTE)
    parser.add_argument('--cli')
    parser.add_argument('--background', action='store_true', help='no focus, off-screen, no audio (automated runs)')
    parser.add_argument('--dry-run', action='store_true', help='check and print, change nothing')
    return parser.parse_args(argv)


def main(argv=None, environ=None, run=subprocess.run, which=shutil.which, execve=os.execve, chdir=os.chdir,
         system=None, machine=None, out=print) -> int:
    args = parse_args(argv)
    environ = dict(os.environ if environ is None else environ)
    try:
        if not is_token(args.model):
            raise Stop(f'The model name "{args.model}" is not usable: it must be one word.')
        if not 0 <= args.calls_per_minute <= 600:
            raise Stop('--calls-per-minute must be between 0 (no limit) and 600.')
        binary = engine_binary(system, machine)
        if not (binary.is_file() and os.access(binary, os.X_OK)):
            raise Stop(f'The game engine is not built yet ({binary}). Build it first (see README).')
        runtime = args.runtime.resolve()
        if not ((runtime / 'fsgame.ltx').is_file() and (runtime / 'database').is_dir()):
            raise Stop(f'{runtime} is not a staged game folder (it needs fsgame.ltx and database/).')
        cli = find_cli(args.cli, environ, which) if args.mode == 'live' else ''
        env, added, removed = game_environment(environ, args.mode, args.model, args.calls_per_minute, cli)
        if args.mode == 'live':
            check_signed_in(cli, env, run)   # with the game's environment: the login the game will use
        try:
            actions, fixes = plan_install(runtime)
        except (freeplay_runtime.ToolError, OSError) as error:
            raise Stop(f'Cannot install the freeplay files: {error}.')
    except Stop as stop:
        out(f'freeplay_play: {stop}')
        return 1

    argv_game = [str(binary), '-agent_bridge', BRIDGE]
    if args.background:
        argv_game += ['-agent_background', '-agent_sound']
    argv_game += ['-xrsim_freeplay', args.mode]
    if args.mode == 'live':
        budget = 'no limit' if args.calls_per_minute == 0 else f'up to {args.calls_per_minute} decisions per minute'
        ai = f'AI: Claude subscription via Claude Code ({cli}), model {args.model}, {budget}'
    else:
        ai = 'AI: recorded mode, no model'
    if args.dry_run:
        out('checks passed: engine, runtime' + (', Claude Code signed in' if args.mode == 'live' else ''))
        for path, action in actions:
            out(f'would {action if action != "up to date" else "leave (up to date)"}: {path}')
        out(f'would run in {runtime}: {" ".join(argv_game)}')
        out('environment additions: ' + (' '.join(f'{k}={v}' for k, v in added.items()) or 'none'))
        out('environment removals: ' + (' '.join(removed) or 'none'))
        return 0
    try:
        install(runtime, fixes)
    except (freeplay_runtime.ToolError, OSError) as error:
        out(f'freeplay_play: Cannot install the freeplay files: {error}.')
        return 1
    out(f'Game: {binary}')
    out(f'Runtime: {runtime}')
    out(ai)
    out('Starting the game (this window now belongs to it).')
    sys.stdout.flush()
    chdir(runtime)
    execve(str(binary), argv_game, env)   # replaces this process; returns only in tests
    return 0


if __name__ == '__main__':
    sys.exit(main())
