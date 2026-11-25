# Open Agentic XRay

Open Agentic XRay is a detached fork of [OpenXRay](https://github.com/OpenXRay/xray-16) that treats the original X-Ray Engine as foundation, not scripture. The goal is a modern, self-testable S.T.A.L.K.E.R. runtime whose Zone is run by AI agents: the population of the Zone decided by language models, on an engine that is stable, has consistent frame times, and later renders natively on Metal and Vulkan.

The name says what it is: the open X-Ray engine, made agentic. Agents build and test it, and agents will run its Zone. The repository short name `opagxray` stands for **Op**en **Ag**entic **XRay**. The project was formerly called OpenXVibeRay; some data-format identifiers (`openxviberay-*-v1`) keep the old name for compatibility.

This is an engine project. It does not include S.T.A.L.K.E.R. game data, assets, or proprietary content.

## Repository

- Home: [github.com/rzgrw/opagxray](https://github.com/rzgrw/opagxray). It is not in OpenXRay's GitHub fork network.
- Upstream relationship: OpenXRay and IXRay are inspiration and selective donors. There is no PR flow upstream; individual fixes are taken in after review, with attribution and license notices preserved.
- Branches: `main` is stable (default branch) and moves only by fast-forward from `dev` after a recorded gate. `dev` is experimental integration. Feature work goes on short-lived branches that are merged then deleted, or archived as `archive/<name>` tags.
- README flow: this `dev` README is the live one (status, newest build routes, known issues). The [`main` README](https://github.com/rzgrw/opagxray/blob/main/README.md) describes the project and the stable code. When `main` fast-forwards to `dev` after a gate, this README becomes the `main` README as is; between gates only descriptive sections of the `main` README may be refreshed (via a docs-only commit on `main` that is then merged back into `dev`, keeping this file), so fast-forwarding stays possible.

## Current Status

Last updated: 2026-11-25. Detailed inventory and evidence: [docs/PLAN_STATUS.md](docs/PLAN_STATUS.md).

**Platforms:** development happens on **Windows 11** and **macOS**. On Windows, the x64 Release build (MSBuild) runs Call of Chernobyl 1.4.22 on **OpenGL**, and the agent bridge lets scripts and coding agents launch, drive and screenshot the game, several instances at a time. The freeplay population AI (below) was built and run live on macOS (arm64, CMake build). Its new sources are listed in the Visual Studio project but have not been built on Windows yet.

**Priority:** on 2026-11-21 the owner chose to run the freeplay population AI ahead of the OpenGL stability and frame-time comparison, which had been the priority until then. That comparison is still open. Native renderer work stays paused.

**Robustness policy:** script and mod-data mistakes should not abort the game; they are logged loudly (`! ...`) and play continues. Real engine-state corruption stays fatal. See [docs/windows-dev-setup.md](docs/windows-dev-setup.md#known-windows-issues) for the fixes so far.

### Works now

- **Freeplay population AI (experimental; macOS).** In Call of Chernobyl's freeplay, every living stalker and mutant gets exactly one AI decision owner: its squad, its mutant pack, or the NPC itself if it has no group. An owner decides where its group goes (stay, move to a site, retreat). The engine checks each decision against the sites it offered the owner, and the game's own NPCs then carry it out. Most of what follows was shown in Cordon on macOS with "recorded" decisions, typed in through the bridge by an operator; the one live-model run so far has its own bullet:
  - **A model has decided in the game (one 15-minute run):** in Cordon on macOS, Claude (`claude-sonnet-5-5`, asked through the Claude Code CLI on a Claude subscription, capped at 5 decisions per minute) answered 75 wakes: 68 decisions accepted, 2 kept the current plan, 5 rejected (3 replies without the required block, 2 chose to stay with no site to stay at). 10 groups then arrived at the site the model chose, at least one of each owner kind. Median reply time was about 7.1 s. Every reply was an owner's first decision: after 15 minutes 108 of the 178 steerable owners still had no decision.
  - **Census:** about 25 s after loading a save, across all levels: 383 living NPCs, 195 owners, 182 of them steerable, 13 whose destination stays with CoC's own scripts, 0 NPCs without an owner.
  - **Decisions move real NPCs:** from the same starting state, one squad was sent to two different sites in two launches, neither of them the site stock CoC would have picked. It reached the Cordon site in 91.5 s, and the other site, in the Marsh, a different map, in 441.8 s. A lone stalker reached its site in 81.3 s. A mutant pack and a lone mutant also arrived, but at sites stock CoC would have picked too.
  - **Group changes:** a pack whose leader was removed kept its owner and objective and arrived; a decision made before a squad split is rejected as stale; a lone NPC that joins a group comes under the group's owner; a destination that becomes unavailable sends the owner back to its previous objective.
  - **Stock CoC scripts are not modified.** The adapter script (`res/coc/gamedata/scripts/xrsim_freeplay.script`) wraps CoC's own target selection while the game runs and restores the stock function when it is switched off or hits a Lua error. It is enabled with the launch flag `-xrsim_freeplay`; see the [Agent Bridge options](#agent-bridge) and [LLM Provider Configuration](#llm-provider-configuration).
  - **Clean runs:** recorded-mode sessions of about 46, 13, 12.5 and 10 minutes ended with no crash and no Lua error, and the regression gate passed with the flag off.
  - **Tools and tests:** `tools/freeplay_runtime.py` clones a private test runtime and installs the adapter; `tools/freeplay_probe.py` runs a census, a single recorded move, or a watch of the decision ledger over the bridge; `tools/freeplay_play.py` starts a session on a Claude subscription with one command. 69 freeplay library test functions (next to 76 + 14 existing xrSim ones) and 57 Python freeplay tool tests pass on macOS.
- **Engine fixes for background test instances:** the NPC sound player no longer crashes when sound is off; `-agent_sound` keeps audio in background mode; background instances present without vsync; on macOS they keep rendering while their window is covered (shown only with the Mac locked and the displays asleep).
- **Stock campfire-story crash, root-caused:** CoC 1.4.22's `sound_theme.script` aborts when an online camp tells one of 45 stories that lack a `shuffle` setting. This is a bug in stock CoC; the original CoC engine aborts at the same line. `tools/freeplay_runtime.py` writes a generated config override for those stories into a runtime's `gamedata/`; the stock script is untouched and no CoC content is in this repository. See [Known Runtime Notes](#known-runtime-notes).
- **Windows x64 build** via `src/engine.sln` (VS 2022 Build Tools, MSVC 14.44). CoC 1.4.22 played on GL (`renderer_rgl`).
- **Agent bridge on Windows and POSIX:** TCP on `127.0.0.1` on Windows (port written to `<appdata>/agent_bridge.port`), Unix socket on macOS/Linux. One client, `tools/agentctl.py`, for both.
- **Background instances (`-agent_background`):** the window never takes focus, stays off-screen and windowed, has no audio device and never pauses when unfocused. With per-instance appdata and ports, several game instances run side by side without touching the user's desktop.
- **Frame-time telemetry (opt-in):** `perf.*` bridge verbs, export on Windows and POSIX, `tools/frame_time_report.py`, provenance sidecars. Measurement infrastructure only; no performance gain is claimed.
- **CoC stability fixes (Windows, build-verified):** key-binding constants are plain Lua tables, so missing keys read as nil instead of crashing; root-cause fix for the ALife "already found in the registry" crash (`teleport_object` no longer registers squad members at a graph vertex when CoC's `TeleportSquad` runs); GL viewport reset after a resolution change; Page Up/Page Down swap and missing DirectInput key names fixed ([script API audit](docs/COC_SCRIPT_API_COMPAT_2026-11-08.md)).
- **AI foundation:** `xrSimCore` world state, deterministic tool log, snapshot/restore/replay; thin LLM harness for squad and mutant-pack actors; asynchronous, cancellable provider wakes; mutant pack spawning through the bridge; canonical Zone state (Gate 1 Task 1). The standalone `xrSimCore` tests built and passed under MSVC on 2026-11-08, before the freeplay tests were added.

### Not done yet

**Freeplay population AI**

- **Live-model decisions rest on one 15-minute run.** It did not get past each owner's first decision, and it did not exercise a blocked destination, a provider outage or a long session. Decisions are handed out oldest first across the player's level and its linked levels, so squads near the player are not asked first; at the default 6 decisions per minute a first pass over about 180 owners takes about 30 minutes. The Anthropic API provider has not been run in the game.
- **Nothing is saved.** Objectives and owner identities are lost when a save is loaded and on every level change (a level change is a load). Activation is a launch flag, not an in-game setting.
- **Not shown yet:** a group near the player (online) arriving at a destination stock CoC would not have picked; the mutant-pack pair of two different destinations; the Cordon to Darkscape move (the squad looped offline in Cordon, not diagnosed); the Cordon–Garbage crossing and any checks beyond Cordon.
- **Known issues:** the memory that stops an owner from choosing a just-refused site again is unit-tested only (no site was refused in the live run); some held squads are locked without a `blocked` report; the probe's arrival rule disagrees with stock CoC's in one case; respawned squads can overfill a site; CoC's "help the actor" logic is still a second destination writer.
- **Performance is measured on one machine only, in background mode without vsync** (305 s each, same save; not a performance acceptance): wall-clock frame time median/p95 20.9/26.6 ms without the flag, 23.7/28.8 ms with recorded mode. The adapter's sync every 5 s takes 14–28 ms on average and up to 58 ms of main-thread time per call.
- Traders such as Sidorovich and the Forester are not enrolled; persistence of companion orders is untested.
- The Windows build of the new sources is unverified.

**Engine and Windows**

- In-game verification of the Windows CoC crash fixes above (build-verified; resolution changes checked windowed/borderless only).
- The OpenGL stability and frame-time comparison: long multi-instance soaks on Windows and accepted frame-time captures.
- Live LLM provider on Windows: the MSBuild route has no libcurl, so provider wakes coast.
- CMake + Ninja build on Windows (unverified; MSBuild is the working route).

**Longer term**

- Authority-spine Tasks 2+, full replacement of the legacy stalker/monster AI, full ALife ownership for LLM packs.
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

**What exists today.** The freeplay population AI is the first piece of this that reaches a real game world. It covers the squad and pack layer: one decision owner per squad, mutant pack or lone NPC across the whole CoC freeplay population; decisions that are bounded commands (stay, move to a site, retreat), validated against what the owner was shown before they commit; and the engine and CoC's own scripts doing the walking, pathing, fighting and animation. A wake scheduler bounds how many owners think at once and never makes a frame wait.

**What is still ahead:** live-model decisions beyond a first short run, individual minds for named stalkers, faction-level and Zone-level agents, ecology, objectives that persist in saves, and continuity across level changes.

### 2. Native renderers

OpenGL is the current host runtime, not the destination. Plan of record is dual native: **Metal on macOS** (menu first frame done, in-game bring-up parked) and **Vulkan on Windows/Linux** (not started). See the [engine roadmap](docs/superpowers/specs/2026-07-05-engine-roadmap-v2.md).

## How this project is built

The engine is developed by AI coding agents under human direction. The owner sets priorities, chooses between designs and decides what merges; agents write the specs, code, tests and status records. Commit trailers, branch names and the docs show which agents: Claude models co-authored most of the fork's commits, and Codex, Kimi and Grok were used for implementation and independent review.

- **Specs, then plans.** A feature starts as a written design in [`docs/superpowers/specs/`](docs/superpowers/specs/), then becomes an implementation plan in [`docs/superpowers/plans/`](docs/superpowers/plans/) with tasks, files, interfaces, tests and review focus points.
- **One agent implements, another reviews.** Plans are executed task by task: each task goes to one agent and its result is reviewed by another, and the coordinator records rulings on deviations. The whole branch then gets a final review. The [freeplay plan](docs/superpowers/plans/2026-11-20-freeplay-population-ai.md) is an example: seven tasks, follow-up rounds F1–F4, and a final review whose findings were fixed in a last wave, all recorded in [PLAN_STATUS](docs/PLAN_STATUS.md#freeplay-population-ai--2026-11-21).
- **Agents test in the running game.** The [agent bridge](#agent-bridge) lets an agent launch the game in a background instance, press keys, run console commands and Lua, read state and take screenshots, so agents check their own work in play, not only in unit tests.
- **Evidence first.** [PLAN_STATUS](docs/PLAN_STATUS.md) says what was run, on which platform, what passed, what failed and what was not exercised. A feature is not called done because its code exists.

## Project history

- **2026-03-29:** the project starts with a [design spec](docs/superpowers/specs/2026-03-29-macos-metal-port-design.md) for a native macOS port with a Metal renderer. The same day, C++ exceptions are disabled on macOS (`b9b78a979`): they cannot pass through LuaJIT's ARM64 trampoline, so a throw during ALife object removal ended the process. Failures become values from then on.
- **2026-03-30 to 07-06:** Metal backend and a GLSL-to-MSL shader pipeline; on 07-06 the first Metal frame, the CoC main menu (`1ed42b1f1`).
- **2026-07-03:** macOS stabilization: crashed processes become killable, logs survive crashes, exit no longer hangs.
- **2026-07-04:** [agent bridge](docs/superpowers/specs/2026-07-04-agent-bridge-design.md) v1 (`9162c534e`), a socket control channel for autonomous testing.
- **2026-07-05:** the roadmap is rewritten several times in one day: Vulkan everywhere, then dual native renderers; the LLM-simulated Zone becomes the centrepiece ([agentic Zone](docs/superpowers/specs/2026-07-05-agentic-zone-design.md)).
- **2026-07-07 to 07-14:** AI foundation: `xrSim` world state, replayable tool log, provider contract and Anthropic transport, actor intent parser, mutant pack spawning through the bridge, asynchronous provider wakes. The OpenGL soak gate is defined; native renderer work is parked in favour of the GL runtime and AI.
- **2026-11-07:** stability foundation and a private CoC runtime stager; frame-time telemetry with provenance sidecars; the fork is detached from OpenXRay's GitHub fork network.
- **2026-11-08:** Windows bring-up in a day: first MSBuild build, TCP bridge transport, background mode, root cause of the squad-teleport crash, resolution-change fixes, CoC script API audit, the `.db` archive unpacker. The project is renamed Open Agentic XRay (earlier name OpenXVibeRay), and `main` is fast-forwarded to `dev`.
- **2026-11-20 to 11-25:** freeplay population AI: [design](docs/superpowers/specs/2026-11-20-freeplay-population-ai-design.md), plan, implementation, live recorded-decision rounds in Cordon on macOS, and a final review fix wave; then a provider that asks the Claude Code CLI, and on 11-25 the first run in which a model decided for the population.

## Supported Targets

- Primary: Call of Chernobyl (1.4.12 base + 1.4.22 patch), OpenGL renderer, on Windows 11 x64 and macOS Apple Silicon.
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

## Play Call of Chernobyl

The repository contains only the engine. Game content comes from **Call of Chernobyl (CoC)**, a free standalone S.T.A.L.K.E.R. mod by Team EPIC. There are no prebuilt binaries yet, so build the engine first ([Windows](#build-on-windows) or [macOS](#build-on-macos)).

### 1. Get the CoC asset packs

Download both from the mod's ModDB page, [moddb.com/mods/call-of-chernobyl](https://www.moddb.com/mods/call-of-chernobyl) (Downloads section), and follow the mod's own terms:

| Pack | What it is | Extract to (example) |
|---|---|---|
| **Call of Chernobyl 1.4** full install (build 1.4.12) | the base game content | `C:\Games\CoC-1.4.12` |
| **Call of Chernobyl 1.4.22** patch | the latest 1.4 patch | `C:\Games\CoC-1.4.22` |

Extract each into its own folder and keep both folders unchanged. Each one has a `database\` directory of `.db` archives. The patch folder must contain `database\config\scripts.db` and `database\config\configs.db`. Never run CoC's own `bin\` folder: its DirectX-era engine DLLs must not be mixed with the modules built from this repository.

Already have a single CoC install with 1.4.22 applied? Pass that one folder as both `--base` and `--patch` below. That plays fine, but such a runtime is not valid for performance measurements.

### 2. Stage a private runtime

The stager copies the `.db` archives (about 8 GB) into a runtime folder of your own, adds this repository's OpenGL shaders and writes `fsgame.ltx`. Saves, logs, screenshots and settings then live in that runtime's `appdata\`, never in the CoC download folders.

```powershell
py tools\stage_coc_runtime.py --base C:\Games\CoC-1.4.12 --patch C:\Games\CoC-1.4.22 --output build\runtime\coc
```

Needs Python 3.11 or newer. Run it again after a CoC update; it keeps your saves and settings.

### 3. Select the OpenGL renderer (Windows)

Open `build\runtime\coc\appdata\user.ltx` and change the first line from `renderer renderer_r3` to:

```text
renderer renderer_rgl
```

On Windows `renderer_r3` loads the legacy DX11 renderer; OpenGL is the supported one. macOS always uses OpenGL.

### 4. Start the game

From the repository root:

```powershell
# Windows: pass the full path to the staged fsgame.ltx
bin\x64\Release\xrEngine.exe -fsltx C:\path\to\opagxray\build\runtime\coc\fsgame.ltx
```

```bash
# macOS: run from inside the runtime folder
REPO="$PWD"; cd build/runtime/coc && "$REPO/bin/arm64/Release/xr_3da"
```

The main menu shows **Call of Chernobyl ver. 1.4.22**. Pick *New game*, then choose a faction and a starting location. Useful options:

- `-nointro` skips the intro videos.
- `-start 'server(<save>/single/alife/load)' 'client(localhost)'` loads a save directly. Use your own named saves.
- The staged `user.ltx` starts 1280x720 windowed with volume muted; change both in *Options*.

If something goes wrong, check `build\runtime\coc\appdata\logs\` and attach that log to the crash report.

To read CoC scripts or configs: `python -I tools/xrdb_unpack.py <coc>/database/config/scripts.db <empty-dir> [scripts/]`.

### 5. Play with AI on your Claude subscription (experimental, macOS/Linux)

Freeplay AI gives every stalker squad, mutant pack and squadless NPC one decision owner; in `live` mode a Claude model picks where each squad goes next. It can run on your Claude subscription through Claude Code, with no API key:

1. Install [Claude Code](https://docs.claude.com/en/docs/claude-code) and sign in once by running `claude` in a terminal (`claude auth status` should report `"loggedIn": true`).
2. From the repository root: `python3 tools/freeplay_play.py` (runtime `build/runtime/play` by default; `--runtime <dir>` for another staged folder, `--dry-run` to see what it would do).
3. The game starts and shows `AI freeplay is on: Claude subscription via Claude Code, model sonnet, up to 6 decisions per minute`. If something is missing, the launcher or the game says what to do in one sentence.

What to expect: each decision is one Claude Code call (a few seconds each) and spends your subscription usage. Decisions come slowly. From a Cordon start about 180 stalker squads and mutant groups on the Cordon and its three neighbouring levels wait for one, in a single queue, oldest first, across all those levels (squads on the player's level are not served first). At the default 6 decisions per minute the first pass over them takes about half an hour (180 / 6 = 30 min); until its first decision a squad stays at the camp it was already assigned to (a squad with none waits in place). After that each owner is asked again at best about every half hour, because a reconsideration queues behind everyone already waiting. In a 15-minute test at 5 per minute, 108 of 178 owners had no decision yet. Raise the budget with `--calls-per-minute N` (more subscription usage) or lower it to spend less. The launcher installs the freeplay adapter and the story-theme fix into the runtime and starts the game from inside it. It is experimental, and decided destinations are not saved yet: after a load, squads start over from stock targets.

## Agent Bridge

The bridge lets a script or agent run, control and screenshot the game. Protocol: [agent-bridge design](docs/superpowers/specs/2026-07-04-agent-bridge-design.md).

| Option | Meaning |
|---|---|
| `-agent_bridge` | Enable the bridge |
| `-agent_bridge_port <n>` | Windows: TCP port on 127.0.0.1; `0` or absent lets the OS pick. The bound port goes to `<appdata>/agent_bridge.port` |
| `-agent_background` | Unattended instance: no focus, off-screen, windowed, no audio, no pause on focus loss, no error dialogs |
| `-agent_sound` | With `-agent_background`: open the audio device anyway (silence it with `snd_volume_eff 0.` / `snd_volume_music 0.` in `user.ltx`); needed for long CoC sessions |
| `-xrsim_freeplay recorded\|live` | Experimental, CoC runtime with the adapter installed by `tools/freeplay_runtime.py`: one AI decision owner per freeplay squad, mutant pack and squadless NPC. `recorded` takes decisions only from `ai.freeplay.decide`; `live` wakes owners through the provider below (`claude_cli` on a Claude subscription or `anthropic` with an API key); one 15-minute `claude_cli` run on macOS is its only live evidence. Status: [PLAN_STATUS](docs/PLAN_STATUS.md#freeplay-population-ai--2026-11-21) |

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
ai.freeplay.mode|begin|sync|retire|event|wake|observe|decide|tick|owners|census|log   (experimental freeplay owners; the CoC adapter calls most of them through xrsim_verb)
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

Experimental freeplay live mode (`-xrsim_freeplay live`, see the options table; [one-command start](#5-play-with-ai-on-your-claude-subscription-experimental-macoslinux)) has two providers. Reference:

| Variable | Meaning | Default |
|---|---|---|
| `XRAY_AGENT_PROVIDER` | `claude_cli` or `anthropic` | unset: `anthropic` when an API key is set, otherwise `claude_cli` |
| `XRAY_AGENT_MODEL` | model ID or alias | `claude_cli`: `sonnet`; `anthropic`: required |
| `XRAY_AGENT_CLI` | Claude Code executable for `claude_cli` (one word: a bare name on `PATH` or an absolute path) | `claude` |
| `XRAY_AGENT_TIMEOUT_MS` | per-call timeout (`claude_cli`: 1000–600000) | `claude_cli` 60000 |
| `XRAY_FREEPLAY_MAX_IN_FLIGHT` | concurrent decisions, 1–8 | `claude_cli` 1, `anthropic` 2 |
| `XRAY_FREEPLAY_CALLS_PER_MINUTE` | decisions handed to the provider (normally one CLI call each) in any rolling 60 s of engine time (`time_global`, real milliseconds while the game runs; not the in-game clock), 0 = no cap | `claude_cli` 6, `anthropic` 0 |
| `XRAY_FREEPLAY_RECONSIDER_MS` | how often an idle owner is asked again, 60000–86400000 | 600000 |
| `XRAY_FREEPLAY_MIN_WAKE_MS` | minimum time between two decisions of one owner, 5000–3600000 | 45000 |

- **`claude_cli` (experimental)** runs the Claude Code CLI found on `PATH` (or `XRAY_AGENT_CLI`) in headless mode, `claude -p --model <m> --tools "" --no-session-persistence --strict-mcp-config --setting-sources "" --system-prompt ...`, with the login of the environment the game was launched from (a subscription login works). The game removes from the CLI's environment the API key and endpoint variables `ANTHROPIC_API_KEY`, `XRAY_AGENT_API_KEY`, `ANTHROPIC_AUTH_TOKEN` and `ANTHROPIC_BASE_URL`, and the variables of a running Claude Code session (`CLAUDECODE`, `CLAUDE_CODE_SESSION_ID` and others); the launcher removes the same list from the game's environment, and `CLAUDE_CONFIG_DIR` is kept. Each decision is one CLI call, at most 6 per minute by default. It runs on a worker thread from the system temp directory, never through a shell; a call that hangs is killed after the timeout. Refusals, in order: `invalid_model`, `invalid_cli`, `cli_unavailable`, `invalid_setting:<NAME>` (no `missing_model`: the default is `sonnet`). macOS/Linux only: on Windows it refuses with `unsupported_platform`.
- **`anthropic`** uses `ANTHROPIC_API_KEY` (or `XRAY_AGENT_API_KEY`) and needs an explicit `XRAY_AGENT_MODEL`; refusals in order: `missing_api_key`, `missing_model` (the source default model ID is not used), `transport_unavailable`, `invalid_setting:<NAME>`. Set `XRAY_AGENT_TIMEOUT_MS` deliberately for a live run.

Any other `XRAY_AGENT_PROVIDER` value refuses with `provider_disabled`. The `ai.freeplay.mode` reply shows the resolved provider, model and settings, and `last_failure=<reason>` while the provider backs off after a failure; the adapter turns both into one on-screen line.

## Tests and Verification

- Standalone xrSim tests (any platform, no engine build): `cmake -S tools/xrsim_standalone -B build-xrsim -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build-xrsim`, then run `xrSimWorldStateTests`, `xrSimAuthoritySpineTests` and `xrSimFreeplayTests` (76, 14 and 69 test functions; each exits non-zero on a failure); `tools/xrsim_standalone/build_and_test.sh` does all of it on macOS/Linux. `xrSimFreeplayTests` has not been built on Windows yet.
- Freeplay tool tests: `python3 -m unittest tools.tests.test_freeplay_runtime tools.tests.test_freeplay_probe tools.tests.test_freeplay_play tools.tests.test_freeplay_adapter_messages` (57 tests; the adapter message test needs `luajit`). The rest of the CoC adapter script has no test suite in the repository.
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
- Running without an audio device (`-nosound`, or `-agent_background` without `-agent_sound`) used to crash in the NPC sound player (null `CSound` in `CSoundPlayer::play`); fixed in `121d5b5ec`, where the player skips the write when sound is disabled.
- CoC 1.4.22 has a stock campfire-story bug that is fatal on every engine, including the original CoC engine: `sound_theme.script` defaults an `npc` theme's `shuffle` to `"rand"`, which its `select_next_sound` does not handle, and 45 `strange_story_*` npc themes have no `shuffle`, so a story told by an online camp aborts at `sound_theme.script:206` (`played_id` nil). It affects an ordinary play folder too. On macOS (2026-11-21) it ended 11 of 15 Task 6 background launches near the Rookie village, all with `-agent_sound` (2 of them without the freeplay adapter); the Task 5 background runs without `-agent_sound` crashed the same way after about 100–150 s of play. Mitigation (`f9e134340`): `tools/freeplay_runtime.py` writes a generated config override with `shuffle = rnd` for those sections into a runtime's `gamedata/` (`clone` always does; for your own runtime run `python3 tools/freeplay_runtime.py install --runtime <runtime> --fix-story-themes`, which also copies the adapter script, inactive without `-xrsim_freeplay`). The stock script is not modified. With the override, two 511 s runs and the regression gate passed. Evidence: `docs/PLAN_STATUS.md` (Freeplay population AI).
- With `-agent_sound`, the log can show `! [LUA]  Playing sound is not completed, but is destroying "<sound>"!` with a stack dump. It is an engine warning from the script sound destructor, not a Lua error.
- macOS `-agent_background` instances render while their window is occluded since `4f6044610`; this was shown only with the Mac locked and the displays asleep.
- `-agent_background` instances present without vsync (`d1d7a0877`), so frame-time captures taken in background mode are not vsync-paced.
- Fullscreen resolution changes on Windows are not yet verified after the viewport fix.

## Repository Layout

```text
src/
  xr_3da/                Main executable entry point (xrEngine.exe)
  xrCore/                Platform abstractions, filesystem, threading, debug
  xrEngine/              Core engine, SDL2, input, console, AgentBridge, FrameTime*
  xrEngine/xrSim/        Thin AI simulation harness and provider bridge
                         (xrSimFreeplay*: freeplay owners, decisions, wake scheduler, ai.freeplay verbs)
  xrEngine/xrSim_script.cpp  xrsim_verb, the Lua entry point the CoC adapter calls
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

res/coc/gamedata/scripts/
  xrsim_freeplay.script  CoC freeplay adapter (inactive without -xrsim_freeplay)

tools/
  agentctl.py            Agent bridge client (socket, tcp:, port file, appdata dir)
  stage_coc_runtime.py   Private CoC runtime stager
  xrdb_unpack.py         X-Ray .db archive unpacker
  frame_time_report.py   Frame-time capture report
  frame_time_provenance.py  Capture provenance sidecars
  frame_overhead*.py     Opt-in frame-overhead probe parser and campaign runner
  freeplay_runtime.py    Clone a private CoC test runtime; install the freeplay adapter
  freeplay_probe.py      Freeplay census, recorded move and ledger watch over the bridge
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

- [Play Call of Chernobyl](#play-call-of-chernobyl): asset packs, staging and launch, step by step.
- [docs/PLAN_STATUS.md](docs/PLAN_STATUS.md): current inventory, shipped refs, open acceptance, freeplay evidence. Read first.
- Freeplay population AI: [design](docs/superpowers/specs/2026-11-20-freeplay-population-ai-design.md), [implementation plan](docs/superpowers/plans/2026-11-20-freeplay-population-ai.md), [status and evidence](docs/PLAN_STATUS.md#freeplay-population-ai--2026-11-21).
- [docs/windows-dev-setup.md](docs/windows-dev-setup.md): Windows toolchain, build, bridge, background instances, known issues.
- [docs/COC_SCRIPT_API_COMPAT_2026-11-08.md](docs/COC_SCRIPT_API_COMPAT_2026-11-08.md): CoC script vs engine API audit.
- [docs/WINDOWS_HANDOFF.md](docs/WINDOWS_HANDOFF.md): transfer context and archive-tag recovery.
- [docs/HANDOVER.md](docs/HANDOVER.md): runtime/AI context and parked Metal resume guide.
- [docs/macos-dev-setup.md](docs/macos-dev-setup.md): macOS build/run/bridge setup.
- [Engine roadmap v2](docs/superpowers/specs/2026-07-05-engine-roadmap-v2.md), [agentic Zone](docs/superpowers/specs/2026-07-05-agentic-zone-design.md), [thin harness](docs/superpowers/specs/2026-07-07-llm-thin-harness-ai-design.md), [authority spine](docs/superpowers/specs/2026-07-14-full-zone-ai-authority-spine-design.md).
- [Frame-time plan](docs/superpowers/plans/2026-11-07-frame-time-measurement.md), [OpenXRay performance audit](docs/OPENXRAY_PERFORMANCE_AUDIT_2026-11-07.md), [physics audit](docs/OPENXRAY_PHYSICS_PERFORMANCE_AUDIT_2026-11-07.md).

## Credits

Open Agentic XRay is built on the work of the [OpenXRay team](https://github.com/OpenXRay/xray-16), the S.T.A.L.K.E.R. modding community, and earlier macOS support work by OpenXRay contributors including vertver and Lnd-stoL.

Development is done through human-AI collaboration with coding agents. The point is not to replace engineers; it is to make a large, old, real engine easier to understand, test, and evolve.

Fan project. Not affiliated with GSC Game World. Follow the official [EULA](https://www.gsc-game.com/eula/) and [Fan Content Guidelines](https://www.gsc-game.com/guidelines/).
