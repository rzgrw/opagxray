# Plan status — 2026-11-08

## main promotion — 2026-11-08

At the user's explicit request `main` was fast-forwarded to `dev` **without the full recorded gate**. Evidence at promotion: MSBuild x64 Release built clean (last full rebuild at `9735397d1`; later merges were docs and the script key table); the staged CoC 1.4.22 runtime reached the main menu on GL through the README's `-fsltx` launch (background instance, screenshot checked); the Windows bridge and `-agent_background` passed their menu-scene two-instance test. Not yet verified in game: crash fixes `482a142de`, `215ec351a`, `d85e8208e`, the PgUp/PgDn swap, and long soaks. The next gate must cover them before the following promotion.

## Windows transfer — 2026-11-07

Current execution is moving to Windows. Read [WINDOWS_HANDOFF.md](WINDOWS_HANDOFF.md) before resuming: it records the reviewed menu repair, final Mac runtime results, WIP provenance/overhead branches, Windows bridge/export/build gaps and transfer recovery instructions. Historical checklist and benchmark claims remain subject to their original platform/evidence limits. Build, test, and live results below are macOS runs unless a 2026-11-08 section names a Windows command. The full engine has still not been built on Windows. The standalone `xrSimCore` tests in the Gate 1 recovery section are the exception.

## First Windows build and playtest — 2026-11-08

x64 Release MSBuild build of `dev` (`4555aa76a` vcxproj inventory, `bec14accc`, `bec540223`) ran CoC 1.4.22 on GL; user verdict "playtest OK". Two crashes found and fixed, **build-verified only, in-game re-check pending**: plain-table key constants (`482a142de`) and the non-fatal ALife graph-vertex duplicate guard plus diagnostic (`215ec351a`). Open: viewport not resized after a resolution change. Runtime staged from the pre-merged CoC install, so it is not measurement-grade. Details and the pending check: [windows-dev-setup.md](windows-dev-setup.md#known-windows-issues). The Windows agent bridge is still a stub; CMake + Ninja on Windows is still unverified.

## Repository and branch policy — 2026-11-07

- The repository is [rzgrw/opagxray](https://github.com/rzgrw/opagxray), now **detached** from OpenXRay's GitHub fork network. This supersedes the earlier "no GitHub detachment has occurred" wording here and in the handoff. OpenXRay and IXRay remain inspiration and selective donors: no PR flow upstream, upstream intake only as reviewed, attributed, license-compatible changes.
- Only two long-lived branches. `main` is stable (GitHub default; `950a7820b` on 2026-11-07) and moves only by fast-forward from `dev` after a recorded gate. `dev` is experimental integration. Feature work uses short-lived branches that are merged then deleted, or archived as annotated tags.
- Archive tags created by the 2026-11-07 cleanup (recover with `git fetch origin tag archive/<name>`, then `git switch -c <x> archive/<name>`): `archive/codex/ai-authority-spine-gate1` → `1d951aa07`; `archive/feat/metal-mr1-ingame` → `caa2e4953`; `archive/rzgrw/frame-overhead-base` → `617957470`; `archive/rzgrw/frame-overhead-reference` → `d603c01d2`; `archive/transfer/windows-2026-11-07` → `d336361d1` (only copy of the `docs/transfer/2026-11-07/` evidence and provenance WIP `8cf2af572`). Recovery status in `dev` (2026-11-08): the transfer tag's evidence pack and provenance WIP, and the frame-overhead probe core/wiring plus probe parser and runner, are selectively integrated (see the frame-time section below); the pinned probe adapter, `tools/_overhead_deps`, Mac-only approved builds, and the archived WINDOWS_HANDOFF restore block remain archived. `codex/ai-snapshot-replay` (patch on `dev` as `99ebfb8b5`), `codex/ai-zone-foundation` and `feat/metal-mr0-first-frame` (ancestors of `dev`) were deleted without tags. Tag `sp1-macos-baseline` is kept. None of the archived tips is a merge candidate.

Reconciled against local `dev` at `950a7820b` (2026-11-07 doc refresh; earlier
inventory baseline was `8984220b6`). Git refs and source presence were checked,
but no build, game, provider, or acceptance test was run for this inventory. **Stability and frame-time consistency on OpenGL are the current priority.** Historical checkboxes are
not a code inventory: unchecked steps can already be implemented, and checked
steps do not establish acceptance on today's runtime.

## Runtime and evidence

- [IXRay foundation](superpowers/plans/2026-11-07-ixray-foundation.md) shipped in
  `8984220b6`: two online empty-group guards, production UI shader-key comparator,
  private CoC stager, and bounded frame-wait/menu sequencing.
- Its recorded verification is historical: 41 Python tests, `xrSimWorldStateTests`
  and `xrUIShaderCacheTests`, and the fresh Cordon save/reload/quit gate with vsync
  off. The original ignored artifacts and execution worktree are now absent;
  the checked-in record is not a current rerun or recoverable screenshot proof.
- Use the exact primary IXRay CoC **1.4.12 base + 1.4.22 patch archives**, then the
  target GL overlay. [macOS setup](macos-dev-setup.md) gives the explicit source,
  private runtime, binary, socket, and artifact paths. Keep user and test appdata
  separate; never inherit the stale Downloads runtime default or assume save `1`.
  Fresh tests start a new game and use their own named save.
- The repository compiler setting is **C++20** in
  [XRay.Build.cmake](../cmake/XRay.Build.cmake). C++17 in historical plan snippets
  describes a portable subset, not a request to downgrade the repository.

## Inventory

“Shipped” below means present in the reconciled baseline, not that every original
acceptance checkbox was verified anew. Commit ranges identify implementation
history rather than instructions to replay every commit.

| Plan / track | Status and local evidence |
|---|---|
| [SP1 macOS baseline](superpowers/plans/2026-03-29-sp1-macos-baseline.md) | Shipped: annotated tag `sp1-macos-baseline` (`13f151f04`, peels to `585dda21f`). |
| [Agent bridge](superpowers/plans/2026-07-04-agent-bridge.md) | Shipped: `9162c534e`, `2a155131f`, `70e9183cf`; `AgentBridge` and `tools/agentctl.py` present. |
| [GL baseline harness](superpowers/plans/2026-07-07-gl-macos-stability-baseline.md) | Shipped: `7a9b032cc`…`76209178d`, stabilization `115c71b49`, foundation `8984220b6`; longer acceptance remains open. |
| [AI foundation](superpowers/plans/2026-07-07-ai-zone-foundation.md) | Shipped: `c8d671514`, `27b7dc9a2`, `b002d9d7a`; coarse WorldState, debug bridge, smoke scenario. |
| [HTTP transport](superpowers/plans/2026-07-07-ai-provider-http-transport.md) | Shipped: `47e4badb1`; optional curl transport and value-return coast behavior. |
| [Thin actor harness](superpowers/plans/2026-07-07-llm-thin-harness-ai.md) | Shipped: `b38c943fb`…`589be8ff9`; intent codec, actors, actuator command ledger, runtime and bridge. |
| [Session mutant packs](superpowers/plans/2026-07-08-mutant-pack-session-spawn.md) | Shipped: `a374e6bc9`, `558f73348`, `0e494f6a9`, observability fix `ccd8713b8`; registry/spawn seam, not complete live AI ownership. |
| [Async actor provider](superpowers/plans/2026-07-14-async-live-actor-provider.md) | Shipped: `9cf56d00c`, `e19a9a47d`, `f66aafd34`, contract hardening `96142b7c0`; fresh hardened-prompt non-coast acceptance still open. |
| [Frame-time measurement](superpowers/plans/2026-11-07-frame-time-measurement.md) | Shipped infrastructure: recorder/reports `e3f273f04`, opt-in GL telemetry + `perf.*` bridge export `a389540d8`, readiness/visibility classification `456808010`, provenance sidecars `90c6e57e6`. No accepted capture or measured gain; Windows export unavailable. Overhead experiment: the common probe core, standalone probe/collector test executables, `overhead.*` bridge hook, run-loop `Boundary()` hook, and opt-in `XRAY_OVERHEAD_PROBE` CMake option (default OFF, zero cost when off) were selectively recovered from `archive/rzgrw/frame-overhead-base`/`-reference` into `dev` (2026-11-08); both test executables build and pass standalone under MSVC 14.44. The strict `xray-overhead-probe-v1` parser (`tools/frame_overhead.py`) and campaign runner (`tools/frame_overhead_runner.py`) came with them; the pinned probe provenance adapter, `tools/_overhead_deps` snapshot, and Mac-only `overhead_approved_builds_v1.json` stay archived because they pin the pre-external-directory contract `90c6e57e6`. No campaign has run and no overhead number is claimed. |
| Menu reload repair | Shipped: `604fb0690` (failure ownership, activation-effect restore, `xrMainMenuReloadTests`). Recorded macOS focused/sanitizer tests and two private live saved-load lifecycles; root cause of the wrong factory type unknown; Windows unverified. |
| [Metal SP2](superpowers/plans/2026-03-29-sp2-metal-renderer.md) | M-R0/menu shipped (`1ed42b1f1`, merge `4dba4ecd1`); M-R1 still parked at unmerged `caa2e4953` (tag `archive/feat/metal-mr1-ingame`). 2026-11-08 assessment: `git merge-tree` onto `dev` `437cca1aa` is clean, but the commit edits shared GL shader sources, so it stays archived. Not built here (no macOS). Resume details in [HANDOVER](HANDOVER.md). |
| [Vulkan](superpowers/specs/2026-07-05-vulkan-renderer-roadmap.md) | Not started; native-renderer work paused. Metal on Mac / Vulkan on PC remains the long-term direction. |
| [Gate 1 authority spine](superpowers/plans/2026-07-14-garbage-ai-authority-spine.md) | Task 1 selectively recovered and integrated at `04b4ef5b5` after independent review. Standalone `xrSimCore` test project taken from `1d951aa07` (`tools/xrsim_standalone`). Tasks 2 onward, live wiring, and the reanimation proposal stay on the archive tag; further AI expansion follows the runtime comparison. |
| [Old roadmap](superpowers/specs/2026-07-04-engine-v2-roadmap.md), [deterministic Zone](superpowers/specs/2026-07-05-zone-simulation-design.md), [streaming](superpowers/specs/2026-07-05-seamless-zone-streaming-design.md), [continuation](superpowers/CONTINUATION.md) | Superseded or parked; retain historical research and surviving invariants, not their execution order. |

## Gate 1 recovery and remaining gates

Tag `archive/codex/ai-authority-spine-gate1` (the former branch) resolves to `1d951aa07` and is
**unmerged**. It contains partial Tasks 1–10 from `facec9eb9` / `3cf0a8543` plus
the standalone test seam at `1d951aa07`. This is recovery material, not a branch
ready to merge or proof that all tasks are executable as written. The separate
`f1222d148` reanimation document's Gemini/model/version-ladder changes are
**PROPOSED, not approved**; they do not amend the current provider contract.

### Selective recovery — 2026-11-08

The 2026-11-08 recovery (merged into `dev`) takes one self-contained piece and leaves the rest on the tags.

Taken from `1d951aa07` (partial): `tools/xrsim_standalone/CMakeLists.txt` and `tools/xrsim_standalone/build_and_test.sh` only. The project configures current `xrSimCore` and the two existing unit-test binaries without the engine. The `<chrono>` / `<thread>` include in that commit targeted the pre-recovery spine tests and was not replayed; Task 1 tests on `dev` already include what they use, and `xrSimWorldStateTests` already includes both headers.

Left on `archive/codex/ai-authority-spine-gate1`:

- `facec9eb9` — Tasks 1–9 substrate plus live bridge, `CLevel`, hit/death, and device wiring. Task 1 is already on `dev` as the repaired `04b4ef5b5` snapshot. Authority, the strict codec, and atomic commit stay queued behind the stability/performance comparison. The same commit also edits `res/gamedata/shaders/gl/ssao.ps` and the shared `cmp_pass` ordering in `r__dsgraph_render.cpp`, which would change GL output, so it is not a default-off substrate drop.
- `3cf0a8543` — live zone runtime, materialize/steer, soak-script changes, and a bridge-only precache fallback in `device.cpp`. That changes runtime behaviour and is not behind an off-by-default switch.
- `f1222d148` — reanimation plan plus HANDOVER and roadmap edits. The plan calls itself a plan of record and proposes the unapproved Gemini and V1 changes named above. It was not copied in. The tag remains the copy. Importing it under a banner would still put a contradictory "plan of record" in the tree.

Windows evidence for the standalone project, 2026-11-08, VS 2022 Build Tools 17.14, MSVC 19.44.35229.0 (`cl.exe` under toolset 14.44.35207), CMake 3.31.6-msvc6, Ninja 1.12.1, curl not found. `vcvars64.bat` printed that `vswhere.exe` was not on PATH, then initialized x64. The full engine was not configured or built.

```text
cmake -S tools\xrsim_standalone -B build-xrsim -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-xrsim --parallel
build-xrsim\xrSimAuthoritySpineTests.exe
build-xrsim\xrSimWorldStateTests.exe
```

Configure reported `Could NOT find CURL`. The build finished (static `xrSimCore.lib` plus both executables). Both executables exited 0 and printed no `FAIL` lines. `xrSimAuthoritySpineTests` `main` runs 14 groups; `xrSimWorldStateTests` `main` runs 76 cases. Either binary returns 1 if any group fails. With curl absent, `IsAnthropicHttpTransportAvailable()` is false and the factory test expects `unavailable`. This rerun does not replace the macOS ASan/UBSan record for `04b4ef5b5`.

Task 1 manifest/canonical state shipped at `04b4ef5b5`: 14 focused groups, 76 existing xrSim cases, 41 tooling tests, and C++17 no-exceptions ASan/UBSan passed; Opus independently rebuilt and approved the final snapshot. This is library-only verification, not live Gate 1 acceptance. Local audit evidence is retained under `artifacts/authority-recovery-2026-11-07/`.

User priority update: IXRay feels more stable and performant. Complete the comparative frame-time/stability investigation before further AI expansion; do not infer performance gains from source differences or average FPS alone. The source comparison is complete; runtime telemetry and selective upstream adoption are now in progress.

Remaining AI order: Task 2 authority →
Task 3 strict codec + atomic commit → ledger/projections/provider → scheduler →
materializer/runtime → replay, soak, and live acceptance. Task 2 can proceed
separately after Task 1; Task 3 first needs explicit reconciliation of
precondition/constraint grammar, scope-subject resolution, create
archetype/ownership grammar, and supply-versus-stock semantics.

Coordinator decisions for that recovery: world epoch advances monotonically only
on successful Bootstrap (failure leaves it unchanged); snapshots cover the full
canonical state; staged commit compares epoch, revision, and the base snapshot.
The lifetime guard comes from local request metadata, with no shared `Handle`
changes. These are recovery requirements, not claims about code on `dev`.

Remaining acceptance includes a hardened live non-coast prompt run, pack
verification on the current private runtime, five-run GL and 20-minute idle
soaks, direct empty-online-group execution, and sustained vsync-on gameplay.
Gate 1 additionally needs its own replay/fault, soak, and live authority gates;
**Gate 1 is not complete**. Existing actor commands on `dev` do not yet steer
spawned objects through the planned authority/materialization runtime.

The [thin-harness design](superpowers/specs/2026-07-07-llm-thin-harness-ai-design.md)
and [authority-spine design](superpowers/specs/2026-07-14-full-zone-ai-authority-spine-design.md)
refine the original [agentic Zone](superpowers/specs/2026-07-05-agentic-zone-design.md).
Sonnet-only remains the authority-spine policy; `claude-sonnet-5` is a historical
plan/source value requiring an official availability check before live use.
No model or provider substitution is approved by this reconciliation, and no
network/provider availability check was performed.

## Metal M-R1 assessment — 2026-11-08

`archive/feat/metal-mr1-ingame` (`caa2e4953`, one commit on M-R0 `1ed42b1f1`, 17 files, +146/−36) was assessed and left archived. `git merge-tree --name-only --merge-base=1ed42b1f1 HEAD caa2e4953` (HEAD was `dev` `437cca1aa`) exited 0 and produced tree `e5579d7dc1672c6907c7097fcf82fa552c84e110`. The diff of that tree against `HEAD` is the same 17 files as the original commit, so the patch still applies cleanly onto `dev` `437cca1aa`.

It was not integrated. Six of the files are `res/gamedata/shaders/gl/*`, which the GL renderer loads. Those are not Metal-only paths and not the shared `xrRender` `USE_METAL` exception. The shader edits are `#ifdef SM_METAL` with the previous GL text in `#else`, and the shared C++ edits (`Blender_Recorder_StandartBinding.cpp`, `r2_R_render.cpp`, `r2_R_sun_support.h`) keep the existing DX11/GL branches, but the menu-quad change in `r2_R_render.cpp` depends on the `stub_notransform_t_menu.vs` `SM_METAL` negate. Taking the Metal-module files alone would split that pair. Native renderer work stays paused. Nothing was built: this machine has no macOS or Metal SDK, and `XRAY_METAL` stays OFF.

## Frame-time measurement and upstream intake

The standalone recorder/report checkpoint is `e3f273f04`. Runtime loop and GL CPU spans, main-thread bridge controls, and exclusive stopped-only export have independently reviewed implementation and a full GL build plus private Cordon functional run. Coordinator reran three CMake test executables and 64 Python tests, with reviewed source/runtime hashes matched. This is measurement infrastructure, not a measured speedup. The current capture intentionally fails acceptance for unknown content/overlay provenance; classification edge cases, an independent overhead reference, repeated performance captures and equivalent donor timing remain open. See the [measurement plan](superpowers/plans/2026-11-07-frame-time-measurement.md).

The [OpenXRay adoption audit](OPENXRAY_PERFORMANCE_AUDIT_2026-11-07.md) and [physics audit](OPENXRAY_PHYSICS_PERFORMANCE_AUDIT_2026-11-07.md) rank selective candidates. Grass shadow culling is a performance experiment; shared-string ownership and smaller GL/game guards are correctness candidates. OPCODE/Jolt replacements are deferred on current evidence. Independence means our own roadmap and release authority while selective future upstream intake remains possible. The GitHub fork relationship has since been detached (repository `rzgrw/opagxray`); no license change has occurred.

### Classification follow-up

Not-ready intervals now use their executed phase rather than a device-loss flag; visibility unavailable/unknown is observed before early-return paths and excluded from steady analysis. Independent Grok review approved the delta. Full GL build, three CMake tests, 67 tooling tests and sanitizers passed in the author run; the coordinator matched source/binary hashes and reran three CMake tests plus 26 focused tests. A fresh sibling private Cordon runtime passed movement/save/reload/inventory/menu/normal quit and PID-targeted hide/restore during loading and gameplay. Six diagnostic captures have no invalid/dropped loops, but still fail asset provenance acceptance. Forced not-ready, indefinite-load, independent device-loss/reset, unknown-visibility API failure and Cocoa-only occlusion remain unexercised live. See `artifacts/frame-time/task-b0c500502419/report.json`; no performance gain is inferred.

### Provenance checkpoint

The standalone seal/attest sidecar is independently reviewed; 40 focused tests, 16 independent probes and a coordinator rerun of all 107 tooling tests pass. It preserves raw capture bytes and separates asset verification from full measurement acceptance. Live runtime/image observation and the overhead-specific adapter remain open, so no historical capture becomes accepted and no performance number is claimed.

The frozen transfer WIP (`8cf2af572` from `archive/transfer/windows-2026-11-07`) is integrated into `dev` (2026-11-08): the agent-bridge socket rule (in-tree endpoint as communication metadata, `association=path-only`, never `live-fd` on Darwin) and the optional runner-owned external `0700` communication directory with the launcher-record `external_communication_directory` field. The WIP contains edits that aim to repair both P2 findings from the last independent review along the reviewer's minimal corrections (not yet verified; the archived wrap-up says they must be rechecked before claiming closure): directory disjointness uses device/inode identity in both directions (case-insensitive aliases included, without lowercasing paths), and seal/attest recheck directory identity, type, mode, uid, and canonical path after the socket observation, raising `stat_race` on drift. On Windows the suite shows zero status changes on the 40 pre-existing tests and one new portable parsing test passing; the 14 new socket/external fixture tests are macOS-only by construction (Darwin temp paths, libproc, POSIX modes). The saved reviewer `external-probes.py` delta review is still owed before closure is claimed, live attestation remains macOS-scoped per WINDOWS_HANDOFF, and a runner adoption of the external-directory contract remains required before any probe campaign.

## Freeplay population AI — 2026-11-21

On 2026-11-21 the user decided to run this AI work ahead of the GL stability comparison. Branch `feat/freeplay-population-ai` (local, not pushed), plan and briefs in `.superpowers/sdd/2026-11-20-freeplay-population-ai/`. Commits: owner registry `4a6da9f65`/`3cedf8c0a`, decision validation `e3efa4ef2`, wake scheduler `1f33b2a7c`, `ai.freeplay.*` verbs and `xrsim_verb` `15fe1cd39`/`a3975d324`, CoC adapter and runtime clone tool `4f93352ed`, adapter fixes `fe0fd47ab`/`b5520741d`/`faafd08f3`/`a718747f6`, sighting ledger `4ad065110`, the Task 6 probe `894c7f83b`, follow-up F1 `f9e134340`/`8eebbd046`/`4f6044610` (below), and the follow-up F2 docs commit that updates this section. Everything below is a **macOS** run (arm64, CMake build); the Windows build of the new sources (`xrSimFreeplay*`, `xrSim_script.cpp`, which are listed in the vcxproj) is unverified.

Engine changes made for background testing in Task 5:
- `121d5b5ec`: the NPC sound player does nothing when sound is disabled. Without this, the null `CSound` write in `CSoundPlayer::play` crashed `-nosound`/`-agent_background` in game.
- `d1d7a0877`: background instances present without vsync (swap interval 0 under `AgentBackgroundMode()`). Frame-time captures taken with `-agent_background` are therefore not vsync-paced.
- `f21294dd4`: `-agent_sound` keeps the audio device open in background mode. The cloned runtime sets game volumes to 0.

Stock CoC crash at `sound_theme.script:206` (`played_id` nil), fatal:
- Root cause (`.superpowers/sdd/2026-11-20-freeplay-population-ai/debug-sound-theme-report.md`): stock CoC 1.4.22 `npc_sound` defaults `shuffle` to `"rand"`, which `select_next_sound` does not handle; 45 of the 52 `strange_story_*` themes in `configs/misc/sound/script_sound_story_and_music.ltx` are `npc` themes without `shuffle`. The original CoC engine aborts at the same line, so this is a stock bug on every engine, and it affects an ordinary play folder as well as test runtimes. It fires when an online camp tells a story.
- Without `-agent_sound` it ended each Task 5 fix-3 background run after about 100–150 s (`task-5-live/fix3-*`, one of them without `-xrsim_freeplay`). With `-agent_sound` it ended 11 of the 15 Task 6 launches (all with `-agent_sound`; 2 without `-xrsim_freeplay`; `artifacts/freeplay-ai/2026-11-cordon/crashes/`, `step8/`).
- Mitigation `f9e134340`: `tools/freeplay_runtime.py` generates a loose config override that adds `shuffle = rnd` to those 45 sections, from the runtime's own `configs.db` (`clone` always; `install --fix-story-themes` on request). The stock script is untouched and no CoC content is in the repo. For your own runtime: `python3 tools/freeplay_runtime.py install --runtime <runtime> --fix-story-themes` (it also copies the adapter script, which stays off unless the game is launched with `-xrsim_freeplay`). Live check: all 52 `strange_story_*` themes report `rnd` (F1 report).

Adapter behaviour to keep in mind: a `hold` keeps a squad on its already assigned smart, so a holding squad may still be finishing travel it had started (including toward another level).

Tests: `python3 -m unittest tools.tests.test_freeplay_probe tools.tests.test_freeplay_runtime` → 15 tests OK (7 probe, 8 runtime). `tools/xrsim_standalone/build_and_test.sh` (Tasks 1–5) was not rerun in Task 6. Evidence: `artifacts/freeplay-ai/2026-11-cordon/` (git-ignored, kept locally; see its README).

- **Census (Step 5): passed.** `tools/freeplay_probe.py census`, exit 0. Results:
  - l01_escape: `npcs=96 owners=50 transferable=46 script_owned=4 unbound=0`.
  - All linked levels: `npcs=383 owners=195 transferable=182 script_owned=13 unbound=0`.
  - All four owner kinds are transferable on l01_escape.
  - The 13 script-owned squads are listed in the evidence README: 4 at Cordon, 9 at the Marsh base and doctor, one of them `random`.
  - No companions exist in the save.
  - Sidorovich and the Forester are `script_trader` objects that the adapter does not enroll at all. This is a missing binding.
- **Recorded moves (Step 6): partly shown.** In each run below, the recorded `move_to` replaced the squad's stock destination and the squad arrived (exit 0):
  - offline pack 349: run A to 4_11 (~560 m away, 234 s);
  - squad 357, online from 51.0 s after the probe started: turned back from its off-level stock target into the Rookie village, 326 s;
  - offline lone stalker 233: 97 s;
  - lone mutant 110: 56 s.
  - Pack 349 run B to 5_4 also exited 0, but it replaced nothing: 5_4 was the pack's stock smart at decide time and the pack stayed 0.24 m from it (`step6/pack-offline-B/track.json`). So the pair ends at different smarts only because run B was a no-op arrival. Each observation offered only the current smart plus one other (measured in F2 Part 1 below).
  - Not shown:
    - the squad pair's run B (the game crashed in the stock bug about 15 s after the decide; rerun in F2 case 1);
    - the online pack run (boar 238: destination switched at once, but no `arrived` event in 900 s; final distance 129 m; rerun in F2 case 2).
- **Membership (Step 7):**
  - **Leader loss: passed.** The same owner id was kept, `mrev` went 1→2, the objective was kept, and the squad arrived.
  - **Split: passed.** The decision returned `stale_membership`, and the moved NPC's census line shows the destination squad's owner. Both squads were `hold:-`, so the objective part does not tell them apart. Stock scripts accepted `unregister_member`/`register_member`.
  - **A lone owner joining a group: not exercised separately** (done in F2 case 4).
  - **Blocked destination:**
    - With the brief's `disabled = true` it was **not** blocked, because CoC's `target_precondition` ignores `disabled`.
    - With `respawn_only_smart = true` it **passed**: a `blocked` ledger line, the squad kept its previous target, and the owner was queued again. In that build the engine objective stayed at the blocked site; `8eebbd046` (F1) now reverts it (live in F2 case 3).
  - Step 7 ran with the actor moved away from the village camp and `g_god on`, to get past the stock crash.
- **Regression gate (Step 8): not passed in Task 6.** Both stock (no-flag) attempts crashed in the stock `sound_theme` bug before the scenario began, so movement, save/load, inventory, menu and quit were not exercised; the adapter was not enabled (no `-xrsim_freeplay`). Passed in F1 (below).
- Environment: from the third launch on, the off-screen window was occluded. The device reported lost, nothing rendered, and the load screen stayed (`precache=60`) while the simulation ran. Screenshots and input were not checked in those runs. Fixed for background instances in `4f6044610` (F1).
- No live model decisions were made; that is Task 7.

### Follow-up F1 — 2026-11-21

Runtime `build/runtime/freeplay-ai-2` (fresh clone), every launch `-agent_background -agent_sound`, Mac locked with both displays asleep. Evidence: `artifacts/freeplay-ai/2026-11-cordon/followup-f1/`; report `.superpowers/sdd/2026-11-20-freeplay-population-ai/followup-f1-report.md`.
- `f9e134340`: the story-theme override above. `python3 -m unittest tools.tests.test_freeplay_runtime tools.tests.test_freeplay_probe` → 24 tests OK (`a-green.txt`).
- `4f6044610`: on macOS, `-agent_background` instances render while their window is occluded (`CHW::GetDeviceState` returns Normal for them before the occlusion check; hidden/minimized still Lost). Shown only with the Mac locked and displays asleep: about 50 minutes over 6 launches, no wedge, every launch quit normally (`b-fix-load.txt`, `b-fix-capture.csv`). Other power states (lid closed, displays awake) are untested.
- `8eebbd046`: a `blocked` event for the owner's current objective site reverts the objective to the one in force before the last committed decision, with `source=hold` (or `hold:-` when there is none). Unit tests in `xrSimFreeplayTests.cpp` (`c-red.txt`, `c-green.txt`); live in F2 case 3.
- Runs, all at `cordon_start`:
  - two 511 s runs without `-xrsim_freeplay`: alive throughout, exit 0, no `sound_theme` error (`d1-run1-*`, `d1-run2-*`);
  - **regression gate passed**: movement, look, sprint/jump/crouch, save, load, inventory, menu and quit, 45 requests, `failures=0`, screenshots rendered (`d2-transcript.txt`, `d2_regression_driver.py`);
  - one 361 s `-xrsim_freeplay recorded` run: alive, exit 0, no adapter warning (`d3-*`).
- The log of the first 511 s run (3×) and of the gate (1×) contains `! [LUA]  Playing sound is not completed, but is destroying "<sound>"!` with a stack dump (`d1-run1-log-scan.txt`, `d2-log-scan.txt`). This is the engine's `CScriptSound` destructor warning, not a Lua error: no `lua_pcall_failed`, the script keeps running.
- These runs show no abort over about 17 minutes beside the Rookie-village camp; they do not show that a story was told.

### Follow-up F2 — 2026-11-21

Same runtime and binary (`4f6044610`), every launch `-agent_background -agent_sound`, recorded mode unless noted; no engine or adapter change. Evidence: `artifacts/freeplay-ai/2026-11-cordon/followup-f2/` (see the evidence README); report `.superpowers/sdd/2026-11-20-freeplay-population-ai/followup-f2-report.md`. Log scans of every launch: no `lua_pcall_failed`, FATAL or `sound_theme` error.

- **Sites on offer (Part 1, `followup-f2/sites/`).** Each transferable `l01_escape` owner was woken and, as soon as its observation was stored, every Cordon smart (20) was classified with a Lua copy of stock `se_smart_terrain:target_precondition`, matched against the real call for every pair.
  - At game time 08:01 (50 owners), sites besides the current smart, min/median/max: squad 0/0/1 (18 owners; 13 had none), pack 2/3/3, lone stalker 0/1/1, lone mutant 2/3/3. The owners' `sites=` matched the raw count for every owner.
  - Rejections over 950 owner–smart pairs at 08:01: population at `max_population` 622 (13 of 20 smarts full), faction not accepted by the smart's `props` 253, stalker `resource`/`territory` time window 6; 69 passed. `respawn_only_smart`, missing `props` and surge: 0. `disabled`, actor level and simulation activities are not checked by this function.
  - After advancing game time to 22:00 with `level.change_game_time` (59 owners at 22:09): squad 0/2/3, pack 9/10/11, lone stalker 1/1/3, lone mutant 9/10/10; listed after `MAX_SITES = 8` (current smart included): pack 7/7/8, lone mutant 7/7/8 (31 owners trimmed). Rejections over 1121 pairs: faction 724, population 33, stalker `base` time window 23; 341 passed.
  - Adapter vs raw: the same-level filter removed 1–4 passing off-level smarts for 18 of 50 owners at 08:01; the current smart is always listed, also when it fails its own precondition (28 owners) or is on another level (5 owners).
  - Population accounting: stock CoC counts only squads without a script target toward a smart's population, and the adapter sets `scripted_target` on every squad it holds. After the time jump every Cordon smart read population 0 with 0–8 squads registered (`t2-populations.txt`); the same jump without the flag kept the counts (`noflag-t*-populations.txt`). Without any jump, after about 15 minutes `esc_smart_terrain_2_12` read 0/6 with 8 squads (`c234-end-populations.txt`). The 22:09 numbers therefore mix time of day with this undercount.
- **Reruns (Part 2).**
  - Squad 357 run B: the observation listed only run A's site (`esc_smart_terrain_2_12`) and the squad's own off-level stock target `mar_smart_terrain_base`. Sent there, online at decide, the squad crossed Cordon→Marsh offline and **arrived** (leader 0.2 m, 818 s). This is a no-op for the destination, so the squad pair still has no B site that differs from both A and the stock target (`case1-squad-B/`).
  - Online boar pack 238 → `esc_smart_terrain_4_11`, nothing changed (actor at `cordon_start`, 122–177 m away, switch distance 250): **arrived** (leader 42 m, 457 s). No member ever had the actor as enemy; the members took a stalker as enemy about 420 s after the decide, shortly before arrival (`case2-pack-online-diag/`). Task 6's non-arrival of the same pack (a member died, no enemy trace) is not explained.
  - Blocked destination, live: pack 238 at `move_to:esc_smart_terrain_4_11` decided `move_to esc_smart_terrain_5_12` with `respawn_only_smart = true` set right after the decide → ledger `wakeReason=blocked site=esc_smart_terrain_5_12`, owner `state=queued objective=move_to:esc_smart_terrain_4_11 source=hold rev=3`, `scripted_target` stayed `esc_smart_terrain_4_11`. **Passed**; flag reset (`case3-blocked/`).
  - A lone owner joining a group: two `SIMBOARD:create_squad` squads at `esc_smart_terrain_1_11`; one cut to a single member (`kind=lone_stalker`, `stay:esc_smart_terrain_2_12`), the other given `move_to:mar_smart_terrain_base`; the last member moved across. Its census line shows the group's owner and `objective=move_to:mar_smart_terrain_base`, and the emptied owner is retired (absent from `ai.freeplay.owners`; stock removed the squad). **Passed** (`case4-join/join.txt`).

Still open after F2:
- save persistence of owners and objectives, and the saved activation setting;
- the Cordon–Garbage crossing (F2 shows one Cordon→Marsh crossing only), and Zone-wide checks beyond Cordon;
- companion order persistence (no companion squads exist in `cordon_start`);
- `script_trader` NPCs (Sidorovich, the Forester) are not enrolled;
- an online `move_to` whose site differs from both an earlier run and the stock target (Cordon offers none to squads at load);
- the cause of Task 6's online-pack non-arrival;
- the population undercount under the adapter (above), which changes what stock `max_population` limits;
- the Windows build of the new sources (`xrSimFreeplay*`, `xrSim_script.cpp`);
- `4f6044610` in power states other than locked with displays asleep;
- autonomous model wakes (Task 7), waiting on a model ID and API key from the user.
