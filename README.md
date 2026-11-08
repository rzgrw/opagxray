# OpenXVibeRay

OpenXVibeRay is a detached fork of [OpenXRay](https://github.com/OpenXRay/xray-16) that treats the original X-Ray Engine as foundation, not scripture. The goal is a modern, self-testable, agent-driven S.T.A.L.K.E.R. runtime: stable and consistent frame times first, then native Metal/Vulkan rendering and LLM-authored world simulation.

This is an engine project. It does not include S.T.A.L.K.E.R. game data, assets, or proprietary content.

## Repository

- Home: [github.com/rzgrw/opagxray](https://github.com/rzgrw/opagxray). It is not in OpenXRay's GitHub fork network.
- Upstream relationship: OpenXRay and IXRay are inspiration and selective donors. There is no PR flow upstream; individual fixes are taken in after review, with attribution and license notices preserved.
- Branches: `main` is stable (default branch) and moves only by fast-forward from `dev` after a recorded gate. `dev` is experimental integration. Feature work goes on short-lived branches that are merged then deleted, or archived as `archive/<name>` tags.

## Current Status

Last updated: 2026-11-08. Detailed inventory: [docs/PLAN_STATUS.md](docs/PLAN_STATUS.md).

**Platform:** development has moved from macOS to **Windows 11**. The x64 Release build (MSBuild) runs Call of Chernobyl 1.4.22 on **OpenGL**, and the agent bridge now works on Windows, so the game can be launched, driven and screenshotted by scripts and coding agents, several instances at a time. macOS results in older docs are historical.

**Priority:** stability and frame-time consistency on OpenGL. Renderer replacement and further AI authority work wait behind it.

**Robustness policy:** script and mod-data mistakes should not abort the game; they are logged loudly (`! ...`) and play continues. Real engine-state corruption stays fatal. See [docs/windows-dev-setup.md](docs/windows-dev-setup.md#known-windows-issues) for the fixes so far; a dedicated `docs/ROBUSTNESS_POLICY.md` is in progress.

### Works now

- **Windows x64 build** via `src/engine.sln` (VS 2022 Build Tools, MSVC 14.44). CoC 1.4.22 played on GL (`renderer_rgl`).
- **Agent bridge on Windows and POSIX:** TCP on `127.0.0.1` on Windows (port written to `<appdata>/agent_bridge.port`), Unix socket on macOS/Linux. One client, `tools/agentctl.py`, for both.
- **Background instances (`-agent_background`):** window never takes focus, stays off-screen and windowed, no audio device, never pauses when unfocused. Combined with per-instance appdata and ports, several game instances run side by side without touching the user's desktop.
- **Frame-time telemetry (opt-in):** `perf.*` bridge verbs, export on Windows and POSIX, `tools/frame_time_report.py`, provenance sidecars. Measurement infrastructure only; no performance gain is claimed.
- **CoC stability fixes:**
  - CoC key-binding constants are plain Lua tables, so missing keys read as nil instead of crashing (`no static 'DIK_NUMPADENTER'`).
  - Root-cause fix for the ALife "already found in the registry" crash: `teleport_object` no longer puts squad members into the graph vertex registry when CoC's `TeleportSquad` runs. The duplicate guard stays as a logged safety net.
  - GL resolution changes: viewport reset on device reset; the window-fit code no longer feeds its own resize back into the saved `vid_mode`.
  - Page Up/Page Down were swapped in the script key table; four missing DirectInput key names added. Audit: [docs/COC_SCRIPT_API_COMPAT_2026-11-08.md](docs/COC_SCRIPT_API_COMPAT_2026-11-08.md).
- **AI foundation:** `xrSimCore` world state, deterministic tool log, snapshot/restore/replay; thin LLM harness for squad and mutant-pack actors; async cancellable provider wakes; live-session mutant pack spawning; canonical Zone state (Gate 1 Task 1). Standalone `xrSimCore` tests build and pass under MSVC.

### Not done yet

- In-game verification of the CoC crash fixes above (all are build-verified; resolution changes verified windowed/borderless only).
- Long multi-instance soaks on Windows (harness in progress) and accepted frame-time captures.
- Live LLM provider on Windows: no libcurl in the MSBuild route, so provider wakes coast.
- CMake + Ninja build on Windows (unverified; MSBuild is the working route).
- Authority-spine Tasks 2+, full replacement of legacy stalker/monster AI, full ALife ownership for LLM packs.
- Native Metal in-game renderer (parked) and Vulkan (not started).

## Direction

### 1. Agentic Zone

> LLM owns decisions. C++ owns embodiment.

`xrMind` agents decide intent, tactics, memory, ecology and off-screen outcomes. `xrSim` and the engine stay thin: state store, validation, physics, pathing, animation, combat contact, spawning, save/load, and safe execution of bounded commands.

- `ZoneAgent` / `FactionAgent`: global tension, raids, faction plans, economy pressure, quest opportunities.
- `SquadAgent`: patrol, trade, threaten, flank, rescue, surrender, retreat.
- `StalkerAgent`: named or player-touched NPCs with memory and personality continuity.
- `MutantPackAgent`: hunger, territory, stalking, ambush timing, retreat, dens.
- `EcologyAgent`: off-screen predator/prey pressure, migration, births, deaths.

The engine never blocks a frame on the model. A late, offline or invalid provider makes the actor coast on its last valid intent and degrade to minimal survival behaviour.

### 2. Native renderers

OpenGL is the current host runtime, not the destination. Plan of record is dual native: **Metal on macOS** (menu first frame done, in-game bring-up parked) and **Vulkan on Windows/Linux** (not started). See the [engine roadmap](docs/superpowers/specs/2026-07-05-engine-roadmap-v2.md).

## Supported Targets

- Primary: Call of Chernobyl (1.4.12 base + 1.4.22 patch), OpenGL renderer, Windows 11 x64. macOS Apple Silicon is the historical baseline.
- Upstream compatibility goals: Call of Pripyat 1.6.02, Clear Sky 1.5.10, Linux.

## Build on Windows

Full toolchain notes and gotchas: [docs/windows-dev-setup.md](docs/windows-dev-setup.md).

Requirements: Visual Studio 2022 (or Build Tools) with "Desktop development with C++" (MSVC v143, Windows SDK), NuGet, git, Python 3.11+ with the `py` launcher.

```powershell
git clone https://github.com/rzgrw/opagxray.git
cd opagxray
git config core.autocrlf false
git submodule update --init --recursive

# Developer PowerShell for VS 2022, x64
nuget restore src\engine.sln
msbuild src\engine.sln -m -p:Configuration=Release -p:Platform=x64
```

Output: `bin\x64\Release\xrEngine.exe` plus modules.

- From a shell that sets `NoDefaultCurrentDirectoryInExePath` (for example Claude Code's Git Bash), xrCore's pre-build step fails; run `env -u NoDefaultCurrentDirectoryInExePath MSBuild.exe ...`.
- New CMake-only sources must also be added to the matching `.vcxproj` (+ `.filters`) while both build routes exist.
- Never mix stock CoC `bin/` DLLs with repo-built modules.

## Build on macOS

```bash
brew install cmake ccache sdl2 lzo libogg libvorbis theora openal-soft jpeg-turbo
git clone https://github.com/rzgrw/opagxray.git && cd opagxray
git submodule update --init --recursive
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_UNITY_BUILD=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build build -j10
```

Binaries go to `bin/arm64/Release/` (not `build/bin/`). On macOS Tahoe beta add `-DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk` and use an explicit `-j10` (`sysctl -n hw.ncpu` has trailing whitespace there). Details: [docs/macos-dev-setup.md](docs/macos-dev-setup.md).

## Run With Game Data

You need original game files or a legal standalone mod install. Stage a private runtime from a CoC install instead of running inside it:

```bash
py tools/stage_coc_runtime.py --base <coc-1.4.12> --patch <coc-1.4.22> --output build/runtime/coc
```

The stager copies only the database archives and generates `fsgame.ltx`; runtime-written config and appdata belong to the staged runtime. On Windows set `renderer renderer_rgl` in `build/runtime/coc/appdata/user.ltx` (the default `renderer_r3` selects DX11). Launch:

```bash
bin/x64/Release/xrEngine.exe -fsltx "<runtime>\fsgame.ltx"
```

To start straight into a save: add `-start 'server(<save>/single/alife/load)' 'client(localhost)'`. Use your own named saves for tests; never assume save `1`.

To inspect CoC scripts or configs: `python -I tools/xrdb_unpack.py <coc>/database/config/scripts.db <empty-dir> [scripts/]`.

## Agent Bridge

The bridge lets a script or agent run, control and screenshot the game. Protocol: [agent-bridge design](docs/superpowers/specs/2026-07-04-agent-bridge-design.md).

| Option | Meaning |
|---|---|
| `-agent_bridge` | Enable the bridge |
| `-agent_bridge_port <n>` | Windows: TCP port on 127.0.0.1; `0` or absent lets the OS pick. The bound port goes to `<appdata>/agent_bridge.port` |
| `-agent_background` | Unattended instance: no focus, off-screen, windowed, no audio, no pause on focus loss, no error dialogs |

`agentctl.py` accepts a Unix socket path, `tcp:HOST:PORT`, an `agent_bridge.port` file, or an appdata directory containing one. `--wait S` retries until the instance is up.

```bash
# Windows, one background instance with its own appdata (Git Bash)
RT=build/runtime/coc
mkdir -p $RT/appdata_a && printf 'renderer renderer_rgl\nvid_mode 1024x576\n' > $RT/appdata_a/user.ltx
sed 's/^\$app_data_root\$.*/$app_data_root$ = true| false| $fs_root$| appdata_a\\/' $RT/fsgame.ltx > $RT/fsgame_a.ltx
bin/x64/Release/xrEngine.exe -fsltx "$(cygpath -w $RT/fsgame_a.ltx)" \
    -agent_bridge -agent_bridge_port 0 -agent_background -nointro &
py tools/agentctl.py --wait 90 $RT/appdata_a hello
py tools/agentctl.py $RT/appdata_a shot

# macOS/Linux
python3 tools/agentctl.py <runtime>/appdata/agent_bridge.sock state
```

For several instances, repeat with a different appdata (`appdata_b`, ...) per instance. Logs, saves, screenshots, `perf/` and `user.ltx` then stay separate.

Core verbs:

```text
hello                      handshake
cmd <console command>      run a console command
lua <expression>           evaluate Lua
key <key> [down|up|tap]    inject a key (no OS focus needed)
mouse <dx> <dy> [buttons]  inject mouse motion/buttons
state                      engine/game state summary
shot                       screenshot to <appdata>/screenshots/
perf.start|stop|reset|status|export   frame-time telemetry
bye                        close the session
```

Run a script of verbs: `py tools/agentctl.py <target> --script tools/bridge_soak.txt`. Older scenario scripts in `tools/*.txt` were written on macOS and may assume save `1`.

## AI Bridge Verbs

```text
ai.reset | ai.status | ai.observe | ai.wake | ai.inject <tool call> | ai.snapshot | ai.log | ai.replay
agent.actor.list | agent.actor.observe <kind> | agent.actor.wake <kind> | agent.actor.poll | agent.actor.commands
agent.pack.spawn <section> <count> [radius_m] | agent.pack.list | agent.pack.observe <id> | agent.pack.wake <id> | agent.pack.commands
```

Example: `agent.pack.spawn dog_weak 3 8` spawns three dogs near the actor as one `MutantPackAgent`. Full ALife group ownership comes after this path is stable.

## LLM Provider Configuration

The deterministic/null provider covers unit tests and bridge smoke tests. Live Anthropic calls need libcurl (CMake `-DXRAY_AGENT_HTTP=ON`, macOS/Linux today) and:

```bash
export XRAY_AGENT_PROVIDER=anthropic
export XRAY_AGENT_MODEL=claude-sonnet-5   # historical source default; verify availability before live use
export XRAY_AGENT_API_KEY=...             # ANTHROPIC_API_KEY also accepted
export XRAY_AGENT_TIMEOUT_MS=5000
```

Missing curl, keys or network return coast values such as `network_adapter_not_linked` or `missing_api_key`; they are never fatal.

## Tests and Verification

- Standalone xrSim tests (any platform, no engine build): `cmake -S tools/xrsim_standalone -B build-xrsim -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build-xrsim`, then run `xrSimAuthoritySpineTests` and `xrSimWorldStateTests`.
- CMake focused targets (`EXCLUDE_FROM_ALL`, build by name): `xrSimWorldStateTests`, `xrSimAuthoritySpineTests`, `xrUIShaderCacheTests`, `xrFrameTimeCollectorTests`, `xrFrameTimeCollectorBoundaryTests`, `xrFrameTimeSessionTests`, `xrMainMenuReloadTests`.
- Python tooling: `py -m unittest tools.tests.test_agentctl`. Some other `tools/tests` cases exercise macOS-only behaviour (AF_UNIX, Mach-O) and fail on Windows.
- Regression gate before merging engine changes: a private-save bridge scenario covering movement, save/load, UI and quit, run in background instances.
- Log scan:

```bash
rg -n "FATAL|SCRIPT RUNTIME ERROR|no static|already at graph vertex|not found at graph vertex|lua_pcall_failed" <appdata>/logs
```

## Platform Constraints

- **macOS: no exceptions through LuaJIT.** `XRAY_EXCEPTIONS=0` on Darwin is intentional: C++ exceptions cannot cross LuaJIT's ARM64 trampoline, so `THROW` compiles to fatal verification. New code returns failures as values, never throws across engine/script/provider boundaries.
- **Windows: exceptions are on in Release** (`src/xray_build.props`), so `THROW` paths that CoC's own engine compiled away are real errors here. This is why data-driven asserts are being reviewed under the robustness policy.
- **macOS OpenGL is capped at 4.1**, the reason for native Metal on Mac and Vulkan on PC.

## Known Runtime Notes

- Some CoC startup warnings are mod-script issues, not engine regressions.
- Running without an audio device (`-nosound`, `-agent_background`) can hit an NPC sound-player null dereference in CoC. A fix exists on an unmerged soak branch; until it lands, background instances may crash when NPCs speak.
- CoC can hit a `sound_theme.script` nil `played_id` bug during campfire sound updates; a local game-data guard may be needed for long sessions.
- Fullscreen resolution changes on Windows are not yet verified after the viewport fix.

## Repository Layout

```text
src/
  xr_3da/                Main executable entry point (xrEngine.exe)
  xrCore/                Platform abstractions, filesystem, threading, debug
  xrEngine/              Core engine, SDL2, input, console, AgentBridge, FrameTime*
  xrEngine/xrSim/        Thin AI simulation harness and provider bridge
  xrGame/                Game logic, ALife, Lua bindings, live object spawning
  xrSound/               OpenAL sound layer
  Layers/
    xrRenderPC_GL/       OpenGL renderer module (working baseline)
    xrRenderGL/          OpenGL hardware abstraction
    xrRenderMetal/       Metal HAL (M-R0 shipped, M-R1 parked)
    xrRenderPC_Metal/    Metal module (opt-in, XRAY_METAL=OFF by default)
    xrRender/            Shared renderer code
    xrRender_R2/         Shared render phases and CRender logic
  engine.sln             MSBuild solution (working Windows route)

tools/
  agentctl.py            Agent bridge client (socket, tcp:, port file, appdata dir)
  stage_coc_runtime.py   Private CoC runtime stager
  xrdb_unpack.py         X-Ray .db archive unpacker
  frame_time_report.py   Frame-time capture report
  frame_time_provenance.py  Capture provenance sidecars
  xrsim_standalone/      Standalone xrSimCore test project
  *.txt                  Bridge scenario scripts

docs/
  PLAN_STATUS.md         Current inventory and open acceptance
  windows-dev-setup.md   Windows toolchain, build, bridge, known issues
  WINDOWS_HANDOFF.md     macOS-to-Windows transfer context
  HANDOVER.md            Runtime/AI context, parked Metal resume guide
  macos-dev-setup.md     macOS build/run/bridge setup
  superpowers/specs/     Architecture specs
  superpowers/plans/     Implementation plans
```

## Important Docs

- [docs/PLAN_STATUS.md](docs/PLAN_STATUS.md): current inventory, shipped refs, open acceptance. Read first.
- [docs/windows-dev-setup.md](docs/windows-dev-setup.md): Windows toolchain, build, bridge, background instances, known issues.
- [docs/COC_SCRIPT_API_COMPAT_2026-11-08.md](docs/COC_SCRIPT_API_COMPAT_2026-11-08.md): CoC script vs engine API audit.
- [docs/WINDOWS_HANDOFF.md](docs/WINDOWS_HANDOFF.md): transfer context and archive-tag recovery.
- [docs/HANDOVER.md](docs/HANDOVER.md): runtime/AI context and parked Metal resume guide.
- [docs/macos-dev-setup.md](docs/macos-dev-setup.md): macOS build/run/bridge setup.
- [Engine roadmap v2](docs/superpowers/specs/2026-07-05-engine-roadmap-v2.md), [agentic Zone](docs/superpowers/specs/2026-07-05-agentic-zone-design.md), [thin harness](docs/superpowers/specs/2026-07-07-llm-thin-harness-ai-design.md), [authority spine](docs/superpowers/specs/2026-07-14-full-zone-ai-authority-spine-design.md).
- [Frame-time plan](docs/superpowers/plans/2026-11-07-frame-time-measurement.md), [OpenXRay performance audit](docs/OPENXRAY_PERFORMANCE_AUDIT_2026-11-07.md), [physics audit](docs/OPENXRAY_PHYSICS_PERFORMANCE_AUDIT_2026-11-07.md).

## Credits

OpenXVibeRay is built on the work of the [OpenXRay team](https://github.com/OpenXRay/xray-16), the S.T.A.L.K.E.R. modding community, and earlier macOS support work by OpenXRay contributors including vertver and Lnd-stoL.

Development is done through human-AI collaboration with coding agents. The point is not to replace engineers; it is to make a large, old, real engine easier to understand, test, and evolve.

Fan project. Not affiliated with GSC Game World. Follow the official [EULA](https://www.gsc-game.com/eula/) and [Fan Content Guidelines](https://www.gsc-game.com/guidelines/).
