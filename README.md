# Open Agentic XRay

Open Agentic XRay is a detached fork of [OpenXRay](https://github.com/OpenXRay/xray-16) that treats the original X-Ray Engine as foundation, not scripture. The goal is a modern, self-testable, agent-driven S.T.A.L.K.E.R. runtime: stable and consistent frame times first, then native Metal/Vulkan rendering and LLM-authored world simulation.

The name says what it is: the open X-Ray engine, made agentic. Agents build and test it, and agents will run its Zone. The repository short name `opagxray` stands for **Op**en **Ag**entic **XRay**. The project was formerly called OpenXVibeRay.

This is an engine project. It does not include S.T.A.L.K.E.R. game data, assets, or proprietary content.

## Repository and Branches

- Home: [github.com/rzgrw/opagxray](https://github.com/rzgrw/opagxray). It is not in OpenXRay's GitHub fork network.
- Upstream relationship: OpenXRay and IXRay are inspiration and selective donors. There is no PR flow upstream; individual fixes are taken in after review, with attribution and license notices preserved.
- `main` (this branch) is stable. It moves only by fast-forward from `dev` after a recorded verification gate.
- [`dev`](https://github.com/rzgrw/opagxray/tree/dev) is experimental integration and where active work happens, currently on **Windows 11** (MSBuild x64 build, OpenGL runtime, Windows agent bridge with background instances).

**About this README.** On `main` it describes the project and the code in this branch. The [`dev` README](https://github.com/rzgrw/opagxray/blob/dev/README.md) carries the live development status, build instructions for the newest platforms, and known issues. When `dev` passes a gate and `main` fast-forwards, the `dev` README comes with it and becomes the `main` README.

## Status of This Branch

Snapshot as of the last gate (status text last updated 2026-07-08). For current progress see the [`dev` README](https://github.com/rzgrw/opagxray/blob/dev/README.md) and [docs/PLAN_STATUS.md on dev](https://github.com/rzgrw/opagxray/blob/dev/docs/PLAN_STATUS.md).

The working runtime is **OpenGL on macOS**, driven through the shipped **agent bridge**. Renderer replacement is paused while the AI rehaul lands on top of the stable GL baseline.

The active AI direction is:

> LLM owns decisions. C++ owns embodiment.

In practice, `xrMind` agents decide intent, tactics, memory, ecology, and off-screen outcomes. `xrSim` and the engine stay deliberately thin: state store, validation, physics, pathing, animation, combat contact, spawning, save/load, and safe execution of bounded commands.

What works now:

- macOS Apple Silicon build and runtime with OpenGL.
- Agent bridge launch/control/screenshot harness via `-agent_bridge` and `tools/agentctl.py`.
- `xrSimCore` world-state, deterministic tool log, snapshot/restore, replay, and value-error validation.
- Thin LLM harness for squad and mutant-pack observations/wakes.
- Optional Anthropic HTTP transport through libcurl when `XRAY_AGENT_HTTP` is enabled and curl is found.
- Live-session mutant pack spawning through the bridge:
  - `agent.pack.spawn <section> <count> [radius_m]`
  - `agent.pack.list`
  - `agent.pack.observe <pack_id>`
  - `agent.pack.wake <pack_id>`
  - `agent.pack.commands`
- A verified live session spawn path for `dog_weak` packs, including pack registration, observation, wake, and accepted actuator command output.

What is not done yet:

- Full replacement of legacy stalker/monster AI.
- Full ALife ownership for spawned LLM packs.
- Async frame-safe provider runtime for frequent live model wakes.
- Native Metal in-game renderer parity.
- Vulkan renderer bring-up.

## Direction

Open Agentic XRay is moving in two big arcs.

### 1. Agentic Zone

The Zone should feel simulated by agents, not by hardcoded behavior trees.

- `ZoneAgent` and `FactionAgent` own global tension, raids, faction plans, economy pressure, and quest opportunities.
- `SquadAgent` owns stalker squad intent: patrol, trade, threaten, flank, rescue, surrender, retreat.
- `StalkerAgent` is reserved for named or player-touched NPCs with memory and personality continuity.
- `MutantPackAgent` owns hunger, territory, stalking, ambush timing, retreat, den behavior, and risk tolerance.
- `EcologyAgent` owns off-screen predator/prey pressure, migration, births, deaths, and local extinction/repopulation.

C++ does not decide these behaviors when an LLM intent can decide them. C++ validates the intent and turns it into safe actuators: move, look, aim, shoot, flee, stalk, regroup, vocalize, loot, spawn, or author memory.

The engine never blocks a frame waiting for the model. If the provider is late, offline, missing a key, or returns invalid output, the actor coasts on the last valid intent and eventually degrades to minimal survival/reflex behavior.

### 2. Native Renderers

OpenGL is the host runtime for current AI work. It is not the destination.

The renderer plan of record is **dual native**:

- **Metal on macOS**: revived Mac renderer path. Menu first-frame is done; in-game bring-up is parked.
- **Vulkan on Linux/Windows**: not started. MoltenVK may be used on Mac as a development vehicle, not as the final Mac runtime.
- **OpenGL**: working fallback and AI development host until native renderers reach parity.

See [docs/superpowers/specs/2026-07-05-engine-roadmap-v2.md](docs/superpowers/specs/2026-07-05-engine-roadmap-v2.md) for the roadmap.

## Supported Targets

Primary development target:

- Call of Chernobyl 1.4.22 on macOS, OpenGL renderer.

Upstream compatibility goals:

- S.T.A.L.K.E.R.: Call of Pripyat 1.6.02
- S.T.A.L.K.E.R.: Clear Sky 1.5.10
- Linux and Windows OpenXRay targets

The AI bridge and current live-session pack work are being developed and verified against Call of Chernobyl first.

## Build on macOS

Install dependencies:

```bash
brew install cmake ccache sdl2 lzo libogg libvorbis theora openal-soft jpeg-turbo
```

Clone and initialize submodules:

```bash
git clone https://github.com/rzgrw/opagxray.git
cd opagxray
git submodule update --init --recursive
```

Configure and build:

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_UNITY_BUILD=ON \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0

cmake --build build -j10
```

Binaries are written to:

```text
bin/arm64/Release/
```

They are not written under `build/bin/`.

On macOS Tahoe beta, use an explicit SDK if Command Line Tools need it:

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_UNITY_BUILD=ON \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk
```

Avoid `cmake --build build --parallel $(sysctl -n hw.ncpu)` on Tahoe. `sysctl -n hw.ncpu` can include trailing whitespace there, so use an explicit value such as `-j10`.

## Run With Game Data

You need original game files or a legal standalone mod install. The engine runs in portable mode: launch it from the directory containing `fsgame.ltx`.

Copy GL shaders into the game data once:

```bash
cp -r /path/to/opagxray/res/gamedata/shaders/gl \
  /path/to/game/gamedata/shaders/gl
```

Launch:

```bash
cd /path/to/game
DYLD_LIBRARY_PATH=/path/to/opagxray/bin/arm64/Release \
  /path/to/opagxray/bin/arm64/Release/xr_3da
```

Launch directly into a saved single-player session:

```bash
cd /path/to/game
DYLD_LIBRARY_PATH=/path/to/opagxray/bin/arm64/Release \
  /path/to/opagxray/bin/arm64/Release/xr_3da \
  -start 'server(1/single/alife/load)' 'client(localhost)'
```

Full macOS setup and troubleshooting live in [docs/macos-dev-setup.md](docs/macos-dev-setup.md).

## Agent Bridge

The bridge is the standard verification harness. Launch with:

```bash
cd /path/to/game
DYLD_LIBRARY_PATH=/path/to/opagxray/bin/arm64/Release \
  /path/to/opagxray/bin/arm64/Release/xr_3da -agent_bridge
```

The default socket is:

```text
<game>/appdata/agent_bridge.sock
```

Drive it with:

```bash
python3 /path/to/opagxray/tools/agentctl.py \
  /path/to/game/appdata/agent_bridge.sock hello
```

Core verbs:

```text
hello
cmd <console command>
lua <expression>
key <key> [down|up|tap]
mouse <dx> <dy> [buttons]
state
shot
bye
```

Run a bridge script:

```bash
python3 /path/to/opagxray/tools/agentctl.py \
  /path/to/game/appdata/agent_bridge.sock \
  --script /path/to/opagxray/tools/bridge_soak.txt
```

Screenshots are written under:

```text
<game>/appdata/screenshots/
```

## AI Bridge Verbs

World-state smoke verbs:

```bash
python3 tools/agentctl.py "$SOCK" ai.reset
python3 tools/agentctl.py "$SOCK" ai.status
python3 tools/agentctl.py "$SOCK" ai.observe
python3 tools/agentctl.py "$SOCK" ai.wake
python3 tools/agentctl.py "$SOCK" ai.inject "adjust_population debug_region blind_dog 500"
python3 tools/agentctl.py "$SOCK" ai.snapshot
python3 tools/agentctl.py "$SOCK" ai.log
python3 tools/agentctl.py "$SOCK" ai.replay
```

Thin actor harness verbs:

```bash
python3 tools/agentctl.py "$SOCK" agent.actor.list
python3 tools/agentctl.py "$SOCK" agent.actor.observe squad
python3 tools/agentctl.py "$SOCK" agent.actor.wake squad
python3 tools/agentctl.py "$SOCK" agent.actor.observe mutant_pack
python3 tools/agentctl.py "$SOCK" agent.actor.wake mutant_pack
python3 tools/agentctl.py "$SOCK" agent.actor.commands
```

Live-session mutant pack verbs:

```bash
python3 tools/agentctl.py "$SOCK" agent.pack.spawn "dog_weak 3 8"
python3 tools/agentctl.py "$SOCK" agent.pack.list
python3 tools/agentctl.py "$SOCK" agent.pack.observe 1
python3 tools/agentctl.py "$SOCK" agent.pack.wake 1
python3 tools/agentctl.py "$SOCK" agent.pack.commands
```

The first pack slice is intentionally small: spawn real game objects near the actor, register them as one `MutantPackAgent`, observe the agent packet, wake it through deterministic or provider-backed intent, and expose accepted commands for verification. Full ALife group ownership comes after this path is stable.

## LLM Provider Configuration

The deterministic/null provider is enough for unit tests and bridge smoke tests. Live Anthropic calls are configured through environment variables:

```bash
export XRAY_AGENT_PROVIDER=anthropic
export XRAY_AGENT_MODEL=claude-sonnet-5
export XRAY_AGENT_API_KEY=...
export XRAY_AGENT_TIMEOUT_MS=5000
```

`ANTHROPIC_API_KEY` is also accepted as a fallback key variable.

The CMake option is on by default:

```bash
cmake -B build -DXRAY_AGENT_HTTP=ON ...
```

If libcurl is unavailable, live provider wakes return a normal coast value such as `network_adapter_not_linked`. Missing API keys return `missing_api_key`. These are not fatal engine errors.

## Tests and Verification

Build the engine:

```bash
cmake --build build -j10
```

Build and run the xrSim test binary:

```bash
cmake --build build --target xrSimWorldStateTests -j10
bin/arm64/Release/xrSimWorldStateTests
```

Run the AI thin-harness bridge script in a live engine session:

```bash
python3 tools/agentctl.py "$SOCK" --script tools/ai_thin_harness_smoke.txt
```

Run the GL macOS soak wrapper:

```bash
python3 tools/gl_macos_soak.py \
  --scenario tools/gl_load_play_save_load.txt \
  --artifacts artifacts/gl_macos_soak/load_play_save_load
```

Useful log scan:

```bash
rg -n "FATAL|SCRIPT RUNTIME ERROR|lua_pcall_failed|invalid agent response|transport_error|agent.pack" \
  /path/to/game/appdata/logs
```

## macOS Constraint: No Exceptions Through LuaJIT

On Darwin, `XRAY_EXCEPTIONS=0` is intentional.

C++ exceptions cannot safely propagate through LuaJIT's ARM64 trampoline. In this repo, X-Ray-style `THROW` paths compile down to fatal verification instead of exception unwinding. New runtime code must return value errors, not throw across engine/script/provider boundaries.

This matters especially for AI and bridge code: invalid model output, bad spawn payloads, missing sections, missing API keys, transport failure, and timeout must all be reportable values.

## Known Runtime Notes

- Do not use `-nosound` for current Call of Chernobyl verification. Some CoC scripts expect sound-theme state to exist.
- CoC can hit a `sound_theme.script` nil `played_id` bug during campfire sound updates. A local game-data guard may be needed while testing live sessions.
- macOS OpenGL is capped at 4.1 and deprecated. Shadow, DDS, and driver quirks are expected until native renderers replace it.
- Some CoC startup warnings are mod script issues, not engine regressions.

## Repository Layout

```text
src/
  xr_3da/                Main executable entry point
  xrCore/                Platform abstractions, filesystem, threading, debug
  xrEngine/              Core engine, SDL2, input, console, AgentBridge
  xrEngine/xrSim/        Thin AI simulation harness and provider bridge
  xrGame/                Game logic, ALife, Lua bindings, live object spawning
  xrSound/               OpenAL sound layer
  Layers/
    xrRenderPC_GL/       Working OpenGL renderer module
    xrRenderGL/          OpenGL hardware abstraction
    xrRenderMetal/       Metal HAL bring-up work
    xrRenderPC_Metal/    Metal module, parked until first-frame gates
    xrRender/            Shared renderer code
    xrRender_R2/         Shared render phases and CRender logic

tools/
  agentctl.py            Agent bridge client
  bridge_soak.txt        Bridge regression script
  ai_zone_smoke.txt      xrSim world-state smoke script
  ai_thin_harness_smoke.txt
  gl_macos_soak.py       Launch/control/log/screenshot soak wrapper

docs/
  HANDOVER.md
  macos-dev-setup.md
  superpowers/specs/     Current architecture specs
  superpowers/plans/     Implementation plans and task slices
```

## Important Docs

- [docs/HANDOVER.md](docs/HANDOVER.md) - current project handover and resume guide.
- [docs/macos-dev-setup.md](docs/macos-dev-setup.md) - macOS build/run/bridge setup.
- [docs/superpowers/specs/2026-07-05-engine-roadmap-v2.md](docs/superpowers/specs/2026-07-05-engine-roadmap-v2.md) - renderer and engine roadmap.
- [docs/superpowers/specs/2026-07-05-agentic-zone-design.md](docs/superpowers/specs/2026-07-05-agentic-zone-design.md) - agentic Zone architecture.
- [docs/superpowers/specs/2026-07-07-llm-thin-harness-ai-design.md](docs/superpowers/specs/2026-07-07-llm-thin-harness-ai-design.md) - current in-game AI authority boundary.
- [docs/superpowers/specs/2026-07-08-mutant-pack-session-spawn-design.md](docs/superpowers/specs/2026-07-08-mutant-pack-session-spawn-design.md) - first live-spawn mutant pack slice.

## Credits

Open Agentic XRay is built on the work of the [OpenXRay team](https://github.com/OpenXRay/xray-16), the S.T.A.L.K.E.R. modding community, and earlier macOS support work by OpenXRay contributors including vertver and Lnd-stoL.

Development is done through human-AI collaboration with coding agents. The point is not to replace engineers; it is to make a large, old, real engine easier to understand, test, and evolve.

Fan project. Not affiliated with GSC Game World. Follow the official [EULA](https://www.gsc-game.com/eula/) and [Fan Content Guidelines](https://www.gsc-game.com/guidelines/).
