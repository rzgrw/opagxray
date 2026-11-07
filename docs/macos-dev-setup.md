# macOS Development Setup Guide

This guide covers building OpenXVibeRay on macOS and staging a private Call of Chernobyl runtime for GL and agent-bridge verification.

## Prerequisites

- macOS 13 or later (Apple Silicon arm64 or Intel x86_64)
- Xcode Command Line Tools (full Xcode is not required)
- Homebrew

Install Xcode Command Line Tools if you haven't already:

```bash
xcode-select --install
```

## Install Dependencies

```bash
brew install cmake ccache sdl2 lzo libogg libvorbis theora openal-soft jpeg-turbo
```

## Build

Initialize submodules first — this step is required before configuring CMake:

```bash
git submodule update --init --recursive
```

Then configure and build:

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_UNITY_BUILD=ON \
  -DCPACK_GENERATOR=ZIP \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build build -j10
```

Discover installed SDKs before choosing a deployment-compatible SDK (do not assume the historical path exists):

```bash
xcrun --show-sdk-path
ls /Library/Developer/CommandLineTools/SDKs
```

For example, when `MacOSX15.4.sdk` is installed:

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_UNITY_BUILD=ON \
  -DCPACK_GENERATOR=ZIP \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk
```

Note: avoid `--parallel $(sysctl -n hw.ncpu)` on Tahoe — `sysctl -n hw.ncpu` produces a trailing space that breaks the argument. Use an explicit `-j10` (or another number) instead.

## Binaries Location

After a successful build, binaries are placed in:

```
bin/arm64/Release/
```

Note this is not `build/bin/` — the final output lives directly under the repo root.

## Running with Call of Chernobyl

### 1. Stage a private runtime

Use Python 3.11+ and legally obtained, extracted CoC base and patch installations.
The following are the exact asset sources used for the IXRay adoption baseline on this machine:

```bash
python3 tools/stage_coc_runtime.py \
  --base '/Users/rz/orca/ixray-1.6-stcop/build/runtime/coc-base/S.T.A.L.K.E.R. - Call of Chernobyl' \
  --patch '/Users/rz/Downloads/S.T.A.L.K.E.R. - Call of Chernobyl 2' \
  --output build/runtime/coc
```

The stager copies `database/**/*.db` and split `.db0`, `.db1`, etc. archives;
patch archives replace same-path base archives. It copies this repository's
`res/gamedata/shaders/gl` as the GL overlay and generates target `fsgame.ltx`
archive aliases for CoC's config/maps/resource/sound/patch directories.
Standard CoC scripts and config defaults come from those archives. No donor
`gamedata_coc`, HLSL, saves, or settings are imported.

`stage-manifest.json` records each managed file's source path, size, and SHA-256.
Files are private copies, never symlinks or hard links. `appdata/` holds the
user config, saves, logs, screenshots, shader cache, and bridge socket;
`gamedata/configs/` is a private writable overlay for CoC script settings.
The initial `user.ltx` selects `renderer_r3`, 1280×720 windowed mode, and muted
volume with the sound subsystem still running.

Stop the game before restaging. Repeating the command preserves all user files
and mutable configs, updates unmodified managed assets, and rejects local edits
to managed files. It refuses nonempty unmanaged outputs, source/output overlap
in either direction, destination symlinks (including ancestors), and hard links.
If the managed asset set shrinks, choose a fresh output instead of retaining
obsolete archives. A restage interrupted after an asset copy can resume when that
manifest-owned file matches the requested source checksum. This recovery applies
only to paths already recorded in the manifest: interruption after adding a new
managed path requires a fresh output or manual recovery, even if its bytes match.
An interrupted first
stage without a manifest stays refused; choose a new empty output. Use physical
output paths: for example `/private/tmp/runtime` instead of `/tmp/runtime` on
macOS, where `/tmp` is a symlink. Sources must remain unchanged while staging runs. The
runtime and assets are ignored by Git; do not commit or redistribute them.

Fixture tests (no proprietary assets required):

```bash
python3 -m unittest tools.tests.test_stage_coc_runtime -v
```

### 2. Launch and verify

From the repository root:

```bash
REPO="$PWD"
cd build/runtime/coc
"$REPO/bin/arm64/Release/xr_3da" -agent_bridge appdata/agent_bridge.sock
```

Use the bridge from a second terminal with the runtime as its working directory:

```bash
python3 "$REPO/tools/agentctl.py" appdata/agent_bridge.sock cmd "main_menu on"
python3 "$REPO/tools/agentctl.py" appdata/agent_bridge.sock state
# Wait for at least two frame advances before capturing, or use gl_menu_smoke.txt.
python3 "$REPO/tools/agentctl.py" appdata/agent_bridge.sock shot private_menu
```

The explicit relative socket path also avoids the macOS Unix-socket length limit
in deeply nested workspaces. Keep the process alive using your terminal or task
runner. Do not use `-nosound`: CoC sound scripts require the initialized subsystem.

## Renderer

Use `renderer renderer_r3` in the private `appdata/user.ltx`. OpenGL is the
working runtime; Metal is parked and is not part of this GL verification gate.
Keep `XRAY_METAL` off and Darwin `XRAY_EXCEPTIONS=0` unchanged.

## Known Issues

**Shadow and lighting visual glitches**
macOS ships an older, deprecated OpenGL 4.1 implementation. Some shadow and lighting effects do not render correctly as a result. This is a platform limitation.

**DDS texture load failures**
Some DDS textures fail to load and produce OpenGL errors. This is caused by limitations in the macOS GL driver, not the engine.

**`sysctl -n hw.ncpu` trailing space on Tahoe**
On macOS Tahoe beta, `sysctl -n hw.ncpu` appends a trailing space to its output. Using `--parallel $(sysctl -n hw.ncpu)` in a build command will fail. Always pass an explicit `-j` value such as `-j10`.

**Non-fatal CoC script warnings**
Warnings about invalid object IDs and inventory slot miscounts appear in the log at startup. These are bugs in the Call of Chernobyl mod scripts, not in the engine, and do not affect gameplay.

## Agent bridge (autonomous testing)

Launch with `-agent_bridge [socket_path]` (default: `<appdata>/agent_bridge.sock`).
Drive it with `tools/agentctl.py <socket> <verb> [payload]` or `--script <file>`.
Verbs: hello, cmd, lua, key, mouse, state, shot, bye — protocol in
`docs/superpowers/specs/2026-07-04-agent-bridge-design.md`.
The bridge sets rs_always_active so the game keeps simulating without window focus.
Example: `python3 tools/agentctl.py appdata/agent_bridge.sock lua 'db.actor.health'`
Acceptance loop: `tools/bridge_acceptance.txt`.

### GL macOS stability gate

The AI phase uses OpenGL (`renderer_r3`) as the stable host runtime. Use the GL soak wrapper to launch the game, drive the bridge, collect logs/screenshots, sample RSS, and write artifacts:

Pass the private runtime explicitly; the historical load/play scenario requires
an existing save named `1` and is unsuitable for a newly staged runtime.

```bash
python3 tools/gl_macos_soak.py \
  --game-dir "$PWD/build/runtime/coc" \
  --binary "$PWD/bin/arm64/Release/xr_3da" \
  --scenario tools/gl_menu_smoke.txt \
  --artifacts artifacts/ixray-foundation/menu
```

For a fresh-game gate, create a new Cordon game through the standard CoC new-game
menu, dismiss the loading screen, verify `level=l01_escape`, exercise movement
and inventory, make a uniquely named private save, reload that save, capture
scene/inventory screenshots, and quit normally through the bridge. Preserve the
scenario and current-run evidence under `artifacts/`; historical soak results
are not evidence for a fresh checkout. Never copy a donor save into this runtime.

The menu scenario explicitly activates the menu and uses `wait_frames 2` before
capturing. This step polls bridge state under the scenario deadline and requires
two advances from its first observed frame. `scene=menu` alone only means that no
level is loaded; it does not prove that the intro has finished or the menu is visible.

Artifacts include `summary.json`, `report.txt`, a copied engine log, bridge state
samples, RSS samples, screenshot status, and pass/fail reasons. The automated
screenshot checks establish freshness and nonempty output, **not image correctness**.
Inspect the saved images for a real world scene, inventory, and menu before claiming
the visual gate passed. Do not interact with the game during an automated scenario.

When preserving a manual playtest, back up `appdata/user.ltx`, `appdata/savedgames/`,
and `gamedata/configs/` first. Prefer a separately staged test runtime so even a
new-game autosave cannot replace the user's autosave. Give the test's explicit
save a unique name. For deeply nested paths, pass a shorter socket name such as
`--socket appdata/b.sock`; macOS Unix socket paths have a small length limit.

A Homebrew `sdl2-compat`/SDL3 session was observed waiting inside
`Cocoa_GL_SwapWindow` with vsync enabled during the initial investigation. The
corrected short menu gate passed with vsync on; the full Cordon gate uses private
`rs_v_sync off` settings. This is not a claim that vsync-on gameplay stability or
native SDL2 parity has been verified. Keep the sound subsystem enabled and restore
temporary settings after verification.
