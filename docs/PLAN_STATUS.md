# Plan status — 2026-11-25

## main promotion — 2026-11-25

At the user's explicit request `main` was fast-forwarded from `20a922587` to `dev` at `82b4be2b5` plus this record, **without the full recorded gate**. What the promotion adds is the freeplay population AI ([below](#freeplay-population-ai--2026-11-21)) and the README refresh. Evidence at promotion, all on macOS arm64: the three `xrSimCore` test programs exit 0 and the 176 Python tool tests pass on `dev` (rerun 2026-11-25 at `aad3d6b7c`; the later commits are README only); the CMake engine build passed and the no-flag regression gate (move, look, save, load, inventory, menu, quit) passed with 0 failures on the build from `ef6f041e9`, after which only the flag-gated adapter, tools and docs changed. Not verified: the Windows MSBuild build of the new `xrSim` sources and of the background-mode engine changes (postponed by the user on 2026-11-25), so `main` may not build on Windows until that is checked; every open item of the freeplay section; and the items the 2026-11-08 promotion left for the next gate (below), which are still not verified in game. Not part of this promotion: the Claude Code CLI provider and its first live-model run, which were on `feat/freeplay-claude-cli` under review at that moment and were merged into `dev` later the same day.

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

On 2026-11-21 the user chose to run this AI work ahead of the GL stability comparison. Every stalker and mutant in CoC freeplay gets one AI decision owner: a squad, a mutant pack, or a squadless NPC. Developed on branch `feat/freeplay-population-ai`, merged into `dev` and `main` on 2026-11-25; plan, briefs, round reports and the coordinator's ledger are in `.superpowers/sdd/2026-11-20-freeplay-population-ai/`. Evidence is in `artifacts/freeplay-ai/2026-11-cordon/` (git-ignored, kept locally; its README indexes every round and marks superseded runs). Everything here is a **macOS** run (arm64, CMake build, private runtime `build/runtime/freeplay-ai-2` cloned from a CoC 1.4.22 play folder). Until Task 8 every decision was a recorded one sent through the bridge; Task 8 (below) is the first live-model evidence: one 15-minute run on the user's Claude subscription through the Claude Code CLI.

### What is on the branch

`git log --oneline dev..HEAD`:
- Engine (`src/xrEngine/xrSim/xrSimFreeplay*`): owner registry `4a6da9f65`, with reused IDs never inheriting a previous mind `3cedf8c0a`; decision validation against the observed sites and the 512-entry decision ledger `e3efa4ef2`; fair bounded wake scheduler `1f33b2a7c`; `ai.freeplay.*` bridge verbs and the `xrsim_verb` Lua entry point `15fe1cd39`; provider coasts become holds with backoff `a3975d324`; sighting wakes without a ledger line `4ad065110`; a blocked destination reverts the objective `8eebbd046`.
- CoC adapter `res/coc/gamedata/scripts/xrsim_freeplay.script` and the runtime clone tool `tools/freeplay_runtime.py` `4f93352ed`; adapter fixes `fe0fd47ab` (log through `log1`), `b5520741d` (release squads on switch-off), `faafd08f3` and `a718747f6` (sighting damping, actor level only; silent clones); steering through stock target selection `1252f3854` and `6cbcfd460` (below).
- Probe `tools/freeplay_probe.py` (`census`, `move`) `894c7f83b`; `watch` (a ledger watch that summarises autonomous wakes per owner kind) `104b9148e`, `c42f9a8fa`; story-theme config override in `tools/freeplay_runtime.py` `f9e134340`.
- Engine changes for background testing: `121d5b5ec`, `d1d7a0877`, `f21294dd4`, `4f6044610` (below). Docs `2b4626437`, `02cdac380`, `d71d6daa2` and this update.
- Final review fix wave (below): engine `ef6f041e9`, probe `9b559a325`, adapter `6f974c496`.

Launch: `-xrsim_freeplay recorded` (decisions only through `ai.freeplay.decide`) or `-xrsim_freeplay live` (provider wakes; since Task 8 the provider resolves itself and `python3 tools/freeplay_play.py` starts it in one command, see below). The rest of this paragraph is the state before Task 8. Without the flag the adapter script loads and registers nothing. Live mode needs `XRAY_AGENT_PROVIDER=anthropic` (also the default when unset), an API key (`XRAY_AGENT_API_KEY` or `ANTHROPIC_API_KEY`) and an explicit `XRAY_AGENT_MODEL`; it refuses otherwise, in the order `provider_disabled`, `missing_api_key`, `missing_model`, `transport_unavailable` (`TestLiveModeRefusalOrder`). With no variables set the refusal is `missing_api_key`. `XRAY_AGENT_TIMEOUT_MS` should be set deliberately for a live run. Task 7 Step 5 (live launch with `XRAY_AGENT_PROVIDER=anthropic` and no key or model): the log shows `! xrsim_freeplay: missing_api_key`, the census is empty (0 owners) and stock target selection is not wrapped (`artifacts/.../task7-step5/`; its README's remark that the unset provider gives `provider_disabled` is wrong, the provider defaults to `anthropic`).

Tests at the branch head (`artifacts/.../final-fix/engine-tests-green.txt`, `tool-tests-green.txt`): `tools/xrsim_standalone/build_and_test.sh` builds and runs `xrSimWorldStateTests`, `xrSimAuthoritySpineTests` and `xrSimFreeplayTests`, all exit 0 with no `FAIL:` line (76, 14 and 52 test functions called from `main`; the freeplay file is new on the branch). `python3 -m unittest tools.tests.test_freeplay_probe tools.tests.test_freeplay_runtime` → 35 tests OK. The adapter has no test suite in the repo; this wave checked it with a stubbed LuaJIT harness kept with the evidence (`final-fix/adapter-harness.lua`, 22 checks). The Windows build of the new sources (`xrSimFreeplay*`, `xrSim_script.cpp`, listed in the vcxproj) is unverified.

### How the adapter steers squads

At activation the adapter wraps the stock target selection, `sim_board.simulation_board.get_squad_target`, at the class level. For a held squad (writer `sim`, enrolled, with an objective) the wrapper answers the objective's smart. Every other call goes to the stock function with its arguments. A new objective revision resets the squad's `current_action` and `__lock` once, so stock asks for a target at the squad's next update, even mid-travel. A smart that fails stock's keep-target check (`target_precondition(squad, true)`) sends one `blocked` event per revision; the wrapper then answers nil, and stock's no-target path holds the squad in place until a new objective arrives. Two cases get nil without an event of their own: a hold whose squad has no assigned smart gets nil and no event at all; a fallback after a blocked objective, when the squad has no assigned smart, gets nil after the `blocked` event for the objective's site was already sent, and no second one (the adapter header says the same). In the F3 `fix1` run `mode off` released 23 adapter-caused locks while 7 `blocked` lines had been sent, so about 16 squads were locked with no event (`followup-f3/fix1-run.log`, `fix1-night-check.json`, `fix1-modeoff.txt`). A Lua error or `ai.freeplay.mode off` restores the stock function and clears the locks the adapter caused. The observation's site list is the current smart first, then smarts on any level that pass the stock precondition and are not locked, nearest first, at most 8; the site distance is stock's game-graph distance (`simulation_objects.sim_dist_to`), not metres, and the wake prompt now says so (`site_fields`, `site_graph_distance` lines; sighting distances are metres). Before the precondition call the adapter skips disabled smarts (stock `target_precondition` does not check `disabled`, but stock's `evaluate_prior` scores a disabled smart 0, so stock never picks one) and smarts the precondition would refuse for certain (respawn-only, full, no props, faction weight); the comment at the adapter's pre-filter says the same. An `arrived` event is never sent for a `stay` or a hold; for a move it is sent unless the squad was already staying at the site by stock's action state (`current_action == 1`) when the revision arrived, so a squad sitting at its smart with another action state can still report an arrival without travel.

**Deviation from the written plan: the `scripted_target` mechanism was replaced.** Up to F2 the adapter held squads by writing `scripted_target`. Stock CoC treats such a squad as story-scripted: `sim_board.script` and `smart_terrain.script` leave it out of smart populations, and `get_script_target()` changes task status, reach rules and dynamic tasks. F2 measured the effect: after a jump to 22:00 every Cordon smart read population 0 with 0–8 squads registered; the same jump without the flag kept the counts (`followup-f2/sites/t2-populations.txt`, `noflag-t*-populations.txt`). F3 verified the replacement:
- 0 squads with `scripted_target` in recorded and no-flag play (`followup-f3/rec-step1-scripted-targets.txt`, `noflag-step1-scripted-targets.txt`);
- populations equal the counted squads on all 20 Cordon smarts before and after the same jump (`rec-pop-t*.txt`);
- hand-back on `mode off` (`fix1-modeoff.txt`, `fix1b-handback.txt`);
- no looping squads across the 19:00 day/night switch: 0 in 13 samples, 7 `blocked` lines, one per owner (`fix1-soak-samples.txt`, `fix1-night-check.json`).

Other deviations from the plan, each ruled in the ledger:
- The adapter logs through `log1`; CoC's `printf` is silent unless `lua_debug` is on.
- `-agent_sound` was added for background runs (engine section below).
- Sighting events come only from `sim` squads on the actor's level. They re-arm beyond 225 m, have a 300 s per-pair cooldown and write no ledger line of their own; the reason appears on the decision line.
- Recorded mode frees the scheduler slot at `observe`.
- A provider-originated coast is a hold with backoff.
- A `blocked` event for the current objective site reverts the objective to the previous one with `source=hold`.
- A held squad whose smart is infeasible holds in place instead of taking a stock destination. During a provider outage such squads stand still until their owner decides.

### Final review fix wave — 2026-11-21

Findings and rulings: `.superpowers/sdd/2026-11-20-freeplay-population-ai/final-review-findings.md`, report `final-fix-report.md`; evidence `artifacts/freeplay-ai/2026-11-cordon/final-fix/`.
- **No-op decisions** (`ef6f041e9`, `6f974c496`): an accepted decision equal to the current objective (verb and site) is ledgered `kept` with no new revision and no `apply` line (memories still stored); `ai.freeplay.decide` answers `kept <verb> <site>`. The adapter sends `arrived` only after travel under a revision. Live (recorded mode, owner 84 staying at `esc_smart_terrain_5_7`): `stay` → `accepted`, `stay` again → `kept` at the same revision, no `arrived` in the next 40 s; then `move_to esc_smart_terrain_2_12` → revision 2, one `arrived` after 343.9 s (`recorded-i1.json`, `recorded-i1-move/`). **Not fixed:** a squad whose own smart fails the keep-target check (at night) still loops: `stay` → `blocked` → revert → wake → `stay` is `accepted` again, because the revert changed the objective (`engine-second-loop-probe.txt`); see Open.
- **Malformed replies** (`ef6f041e9`): a provider result with `ok=false` (the reply's body or text did not decode or parse) is `source=live verdict=malformed:<reason>` and only that owner waits its minimum interval; provider coasts with a reason and queue failures stay holds with the global backoff. A reply from a world dropped by `begin` is `stale_world` first, with no scheduler effect.
- **Watch proof rule** (`9b559a325`): an owner is proven only by an `arrived` event at the site of its latest accepted live `move_to`/`retreat`. `move` retries its baseline ledger read and ends `baseline_failed` before deciding if it cannot read it.
- **Adapter cost** (`6f974c496`): the squadless scan runs 2048 IDs per tick (one whole pass at activation); deaths share one forced sync at most every 2 s; sync reuses a condlist pick made by the squad's own update within the sync period; refused syncs are logged with a count once a minute.
- **Regression gate: passed** without the flag, adapter and engine of this wave: movement, look, sprint/jump/crouch, save, load, inventory, menu, quit, `failures=0`, rendered screenshots; one `Playing sound is not completed` engine warning (`gate-transcript.txt`, `gate-log-scan.txt`).
- **Recorded run, about 13.0 min in play (13.5 min from launch to process exit, `recorded-transcript.txt`): clean.** No crash, no Lua error, no `! xrsim_freeplay` line; census `381/193/180/13/0` (`npcs/owners/transferable/script_owned/unbound`), 0 squadless NPCs in the world and in the census, no `l01_escape` smart over its maximum. The pre-filter gave the same site lists as the precondition alone for all 193 squads (0 mismatches) with 791 instead of 14878 precondition calls (`recorded-checks.json`).
- **Frame time** (`frame-time-summary.txt`; one machine, background instances without vsync, all loops flagged `device_unavailable`, so figures are over all loops; not an acceptance of performance). 305 s each, same save and actor position. No flag: wall median/p95/p99/max 20.9/26.6/29.9/214.4 ms, FrameMove 3.0/5.7/7.4/207.3 ms. Recorded: wall 23.7/28.8/34.2/165.3 ms, FrameMove 3.8/6.8/12.7/158.2 ms. With the flag, 61 loops had a FrameMove of 20 ms or more, about every 5.05 s: the 5 s sync. Temporary instrumentation put the sync at 14–28 ms mean and up to 58 ms per call, about half of it the sighting pass; the old one-frame scan cost 74 ms every 60 s, the spread scan up to 12–17 ms per tick during a pass.

### Engine changes for background testing

- `121d5b5ec`: the NPC sound player does nothing when sound is disabled. Without it, `-nosound` and `-agent_background` crashed in game on a null `CSound` in `CSoundPlayer::play`.
- `d1d7a0877`: background instances present without vsync (swap interval 0 under `-agent_background`). Frame-time captures taken with `-agent_background` are therefore not vsync-paced.
- `f21294dd4`: `-agent_sound` keeps the audio device open in background mode. Cloned runtimes set both volumes to 0.
- `4f6044610`: on macOS, `-agent_background` instances render while their window is occluded; hidden or minimized windows still report the device lost. Shown only with the Mac locked and both displays asleep: about 50 minutes over 6 launches (`followup-f1/b-fix-*`). Other power states are untested.

### Stock campfire-story crash

- **Root cause** (`debug-sound-theme-report.md`): CoC 1.4.22 `sound_theme.script` defaults an `npc` theme's `shuffle` to `"rand"`, which `select_next_sound` does not handle. 45 `strange_story_*` npc themes in `configs/misc/sound/script_sound_story_and_music.ltx` have no `shuffle`, so a story told by an online camp aborts at `sound_theme.script:206` (`played_id` nil).
- **Scope.** The original CoC engine aborts at the same line. This is a stock bug on every engine, and an ordinary play folder has it too.
- **Where it hit.** Without `-agent_sound` it ended each Task 5 fix-3 background run after about 100–150 s (`task-5-live/fix3-*`, one of them without `-xrsim_freeplay`). With `-agent_sound` it ended 11 of the 15 Task 6 launches, all of which used `-agent_sound` (`crashes/`, `step8/`).
- **Mitigation `f9e134340`.** `tools/freeplay_runtime.py` generates a loose config override from the runtime's own `configs.db` that adds `shuffle = rnd` to those 45 sections. `clone` always applies it. For your own runtime: `python3 tools/freeplay_runtime.py install --runtime <runtime> --fix-story-themes`. This also copies the adapter script, which stays inactive without `-xrsim_freeplay`. The stock script is untouched and no CoC content is in the repo.
- **Since the override.** F1–F4 launches beside the Rookie village had no `sound_theme` abort (`followup-f1/d1-*`, `followup-f4/logs-scan.txt`).

### Evidence

**Current (F4, adapter `6cbcfd460`, engine binary `4f6044610`; `followup-f4/`, report `followup-f4-report.md`).** All launches used recorded mode at `cordon_start` with time factor 10 and no spawning. Actor teleports and `g_god on` are disclosed per run in the evidence README.
- **Census: passed.** About 25 s after load: l01_escape `npcs=96 owners=50 transferable=46 script_owned=4 unbound=0`; all levels `383/195/182/13/0`. The 13 script-owned squads are the same as in Task 6 (`census-l01/`, `census-all/`).
- **Squad A/B pair (offline): passed.** Squad `bandit_sim_squad_novice20199`, current smart and stock-assigned target `5_9`, stock pick `gar_smart_terrain_3_5`. A: `esc_smart_terrain_7_11`, arrived in 91.5 s (`L1/squad-offline-A`). B, in a fresh launch: `mar_smart_terrain_6_11`, arrived in 441.8 s after crossing to k00_marsh (`L3/squad-offline-B`).
- **Pack A/B pair (online): A passed, B not shown.** Pack `simulation_boar20306`.
  - A: online at decide, offline from about 442 s; `esc_smart_terrain_4_11` arrived in 457.1 s (`L2/pack-online-A`). A equals what the stock function would have picked at that moment.
  - First B attempt (`L3/pack-online-B`): stock counted the pack as arrived 5 s after the switch, because it shared the smart's game-graph vertex. The leader was 163.8 m away, so the probe exited 1. Its site `8_10` was also the stock pick at decide time.
  - Second B attempt (`L4b/pack-online-B2`): the pack switched to `9_7`, which differs from its smart `5_4` and the stock pick `8_10`, and all three members died.
  - Online mutants walked at about 0.25 m/s. Two further online mutant runs died near the Rookie village (`L1/`). One of them, pack `simulation_boar20083` (`L1/pack-online-A`), had a site (`5_12`) that differs from its smart `2_14` and the stock pick `4_13`.
  - So no online run in F4 arrived at a decided site that differs from both the current smart and the stock pick. The two online runs with such a site (L1 pack 238, L4b) died; the online runs whose site was the stock pick either arrived (pack A; the L3 B attempt by stock's rule only) or timed out (L2 lone mutant) (`runs-summary.json`, `stock_query` per run).
- **Single moves: passed.** Lone stalker `7_11` in 81.3 s (`L2/lone-stalker`; stock pick `gar_smart_terrain_3_5`). Lone mutant `4_11` in 106.8 s, offline, on the third attempt: the first died near the village and the second stalled online in a fight (`L1/`, `L2/`, `L3/lone-mutant-retry`). `4_11` was also the stock pick at decide time, so the lone mutant run does not separate the decision from stock choice.
- **Off-level move.**
  - Cordon→Marsh: arrived (squad pair B above).
  - Cordon→Darkscape (`ds2_lager_st`): **not shown** (`L2/offlevel`). After a fight, the offline squad cycled through the same three Cordon positions, (68.6,161.7) → (91.3,188.9) → (120.4,220.4) and back. It returned to (68.6,161.7) every 71–81 s of wall clock (samples every 10 s, wall-clock timestamps in `after-timeout.txt`), about 12–13.5 game minutes at time factor 10. It was seen offline in this loop from 09:05:38 (probe t=649.8 s in `track.json`) to the last sample at 09:20:13 wall clock: about 14.5 minutes of wall clock, about 2.4 game hours. A counting wrapper saw 0 target selections for the squad in a 90 s window that ended at 09:22:08, after the last loop sample (`selection-count.txt`); selection was not counted for the rest of the loop. This points to offline travel rather than the adapter's answer. Not diagnosed.
- **Leader loss: passed.** Pack `simulation_mix_dogs21602`: commander removed 30 s after the decide. Same owner 441, `mrev` 1→2, members 3→2, objective kept, arrived in 103.6 s (`L2/leader-loss`). The site `4_9` was also the stock pick at decide time.
- **Save and load: no crash, but the decision is lost.** A squad travelling to `mar_smart_terrain_6_11` was saved (`saveload/`). Both a same-process load and a fresh-launch load had no crash, no Lua error and no `! xrsim_freeplay` line. The adapter re-activated (epoch 2 in the same process), the stock function was wrapped exactly once, the 13 non-held squads got the stock answer, and the census was complete. The decision did not survive: stock saves neither `assigned_target_id` nor `current_action`, so on load stock picked `esc_smart_terrain_1_11` and the adapter enrolled the squad as a new owner holding there.
- **10-minute recorded soak: passed** (`L6-*`). No crash, no Lua error, no `! xrsim_freeplay` line; census `381/193/180/13/0`. Every Cordon smart was at or below its maximum, with population equal to the counted squads.
- **Crash.** One launch (L4) vanished before any decision, in a stock `xr_danger` → `accessible_nearest` path (`L4-crash-excerpt.txt`). Not adapter-related as far as the trace shows; retried once.

**Superseded.** These were taken with the old `scripted_target` mechanism:
- Task 6 Step 6 moves (`step6/`);
- Task 6 leader loss and blocked runs (`step7/`);
- F2 Part 2 cases 1–3 (`followup-f2/case1-3*`).
The Task 6 census numbers are reproduced unchanged by F4.

**Membership cases and the round that showed each:**
- **leader loss:** F4 (current; Task 6 earlier with the old mechanism);
- **split with a stale decision:** Task 6 Step 7. The decision returned `stale_membership` and the moved NPC took the destination squad's owner (`step7/split-join.txt`). This is registry behaviour and still valid.
- **lone owner joining a group:** F2 case 4 (`followup-f2/case4-join/join.txt`). Also registry behaviour, still valid.
- **blocked destination:** Task 6 (`respawn_only_smart`; `disabled = true` does not block in CoC; old mechanism, superseded). Live revert in F2 case 3 (old mechanism) and, with the current wrap mechanism, in F3 `blocked-499*` (adapter `1252f3854`, `respawn_only_smart` set on the decided `mar_smart_terrain_6_11`). Infeasible smart held in place in F3 `fix1-*` (adapter `6cbcfd460`).

**Regression gate and soaks.**
- **F1 gate: passed** without the adapter flag, binary `4f6044610`. It covered movement, look, sprint/jump/crouch, save, load, inventory, menu and quit: 45 requests, `failures=0` (`followup-f1/d2-transcript.txt`). It also had two 511 s no-flag runs and one 361 s recorded run with the old adapter.
- **Changes since F1:** the adapter (`1252f3854`, `6cbcfd460`, `6f974c496`), the engine fix wave `ef6f041e9` and docs. The gate was rerun in the fix wave and passed (above).
- **Recorded soaks with the new adapter:** 46 min (`1252f3854`, `followup-f3/rec-*`), 12.5 min (`6cbcfd460`, `fix1-*`) and F4's 10 min. All were clean.

### Task 8: Claude Code CLI provider, call budget, blocked-site memory, one-command start — 2026-11-25

Requested by the user on 2026-11-24 ("can u use my sub not api"; then "the process of connecting the subscription must be simple"). Developed on branch `feat/freeplay-claude-cli`, merged into `dev` on 2026-11-25 after its task review. Brief and report: `.superpowers/sdd/2026-11-20-freeplay-population-ai/task-8-brief.md`, `task-8-report.md`; evidence `artifacts/freeplay-ai/2026-11-cordon/task8-cli/` (its `README.txt` indexes the files).
- **`claude_cli` provider** (`6feeee88b`): each live decision is one `claude -p --model <m> --tools "" --no-session-persistence --strict-mcp-config --setting-sources "" --system-prompt ...` call with the wake prompt on stdin, on the provider queue's worker thread. The POSIX runner spawns the argument vector without a shell, in its own process group and the temp directory, with three pipes and no other inherited descriptor and no API key variable; a deadline (60 s default) bounds each call and a timed out or cancelled child group is killed (SIGTERM, then SIGKILL after 500 ms) and reaped. Results: not started `cli_unavailable`, timeout `cli_timeout`, non-zero exit `cli_exit` (holds with backoff); no intent block or a bad one `malformed:<reason>`. Windows gets a stub.
- **Call budget and settings** (`6d5c8a604`): `XRAY_FREEPLAY_MAX_IN_FLIGHT`, `XRAY_FREEPLAY_CALLS_PER_MINUTE` (rolling 60 s of tick time; hand-outs awaiting their observation count), `XRAY_FREEPLAY_RECONSIDER_MS`, `XRAY_FREEPLAY_MIN_WAKE_MS`, read when the mode switches to live; `claude_cli` defaults to 1 in flight and 6 calls per minute; a bad value refuses with `invalid_setting:<NAME>`; the mode reply echoes provider, model and settings.
- **Blocked-site memory** (`5b6868c26`, closes the parked I1 second loop): a `blocked` event remembers the site per owner for 600 s (cleared by `arrived`, retire, `begin`); a live or recorded decision for it is `recently_blocked` with no new revision; the wake prompt names it; a repeated report of the same site wakes nobody.
- **Simple start** (`14bb936be`, `3201ca817`, `3ec2ec536`): with `XRAY_AGENT_PROVIDER` unset live mode uses `anthropic` when an API key is set and `claude_cli` otherwise, model `sonnet` by default; the mode reply adds `last_failure=<reason>` during a provider backoff; the adapter shows one plain on-screen line on activation, on refusal (what to do) and once per distinct provider failure; `python3 tools/freeplay_play.py` checks engine, runtime and the Claude Code login (`claude auth status`, no model call), installs the adapter and the story-theme fix, and starts the game.
- **Tests:** `xrSimFreeplayTests` 67 test functions (52 before; new: 7 for the provider and the real process runner, 2 for settings and the cap, 4 for the blocked-site memory, 2 for provider resolution and `last_failure`), `xrSimWorldStateTests` 76 and `xrSimAuthoritySpineTests` 14, all exit 0; `python3 -m unittest discover -s tools/tests -t .` 194 OK (15 launcher and 3 adapter-message tests new). No unit test calls the CLI; the process runner test uses `/bin/cat`, `/bin/sleep`, `/bin/sh` and `/bin/pwd`.
- **Live (macOS, `build/runtime/freeplay-ai-3`, launcher with `--model claude-sonnet-5-5`):** smoke run: live mode on with `claude_cli`, first decisions `accepted` (`smoke/`). Direct start with only `-xrsim_freeplay live` and no variables: `mode=live provider=claude_cli model=sonnet ...` (`direct/mode.txt`). 15-minute run at **5** calls per minute (lowered from the default 6 to keep the task inside its budget of about 90 CLI calls; about 86 were made in all): `watch` exit 0, all four owner kinds proven by an arrival at a decided site; 75 live replies in the watch ledger (`run15/watch/ledger.txt`, the 900 s window): 68 accepted, 2 kept, 2 `no_site`, 3 `malformed:no_intent_block` (the ledger read after the watch, `run15/ledger-end.txt`, has one more reply, accepted: 76 and 69), 0 holds, 0 `recently_blocked`, 0 `blocked` events, 10 arrivals, 5 casualties; reply latency median about 7.1 s, maximum 13.5 s; 75 replies in the 900 s watch, i.e. 5.0 per minute, the cap (one call at a time at about 7 s each could make about 8.5); queue age median 5.4–9.7 min per kind; the maximum, 14.8 min, is the length of the run, not a bound on waiting. Every one of the 75 replies answered a first wake (`wakeReason` `spawn` 45, `spawn,sighting` 30): the run never got past the first pass. Owners: the first sync enrols all 670 squads in `SIMBOARD.squads` (`synced owners=670`); stock CoC disables the squads on levels not linked to the actor's at their first update and the next syncs retire them, leaving 195 owners at the start of the watch and 191 at its end, on five levels (Cordon, Marsh, Garbage, Darkscape, and one owner that had moved on to Agroprom). 182 and 178 of them are transferable (writer `sim`), the only ones that get decisions. At the end (`run15/watch/owners.jsonl`, last line) 107 transferable owners had never had a decision (26 of them on the Cordon) and 1 was awaiting one. Quit with a CLI call in flight (twice): the game exited in 2.7 s and 3.2 s, no `claude` process left. No crash, no Lua error, no `! xrsim_freeplay` line (one known `Playing sound is not completed` engine warning); `l01_escape` smarts at the end all at or under their maximum (`run15/populations-end.txt`). Limits: one run, one machine, background mode, Cordon start only; the activation message was not captured on screen (the screenshot came after its 10 s); the fix for the blocked loop was not exercised live.
- **Fix round 1** (`45a5d8353`, `fac5c97dc`, docs): the CLI child's environment drops the same variables the launcher drops (API key and endpoint variables including `ANTHROPIC_AUTH_TOKEN` and `ANTHROPIC_BASE_URL`, and the Claude Code session variables; one list in each place, a test keeps them identical); the reap after SIGKILL is polled for at most 3 s, then the child is left with one `~ xrsim: claude_cli: ...` log line; on Windows `claude_cli` refuses with `unsupported_platform`; the `missing_api_key` and `missing_model` refusals say what to do; the launcher checks the Claude Code path it found with the engine's rule, quotes its `--dry-run` command and reports an exec failure in one sentence. Tests: `xrSimFreeplayTests` 69 functions, tools 198 OK. One measured CLI call with the engine's arguments plus `--output-format json` (`task8-cli/fix1/token-measure/`): 642 input tokens (2 uncached, 640 cache creation) and 20 output tokens for a 9-word prompt, so no user instruction, memory or settings files are loaded into the calls; the arguments are unchanged.

### Destination choice

- **At load (F2, game 08:01, `followup-f2/sites/t1.json`).** This counts sites besides the current smart, before off-level sites were offered. A Cordon stalker squad had 0/0/1 (min/median/max; 13 of 18 had none), packs 2/3/3, lone stalkers 0/1/1 and lone mutants 2/3/3. Of 950 owner–smart pairs, 622 were rejected at `max_population` (13 of 20 smarts were full), 253 for faction and 6 for time of day.
- **With the cross-level list (F3, about 8 min after load, `followup-f3/sites-t1.json`).** This counts total sites including the current smart: squads 1/3/6, packs 1/4/5, lone stalkers 4/5.5/6, lone mutants 2/4/5. The off-level sites offered are Marsh, Garbage and Darkscape smarts. No owner reached the cap of 8.
- **Pack timing (F4).** A pack's list at load held only its stock smart and `4_11`. In L3 (playing from 09:23:02 wall clock, time factor 10, polled about every 12 s) `8_10` appeared at 09:27:49, about 48 game minutes in, and `9_7` at 09:30:51, about 78 game minutes in; L4b shows the same timing (`followup-f4/L3-`, `L4b-pack349-sites-poll.txt`).

### Open

**From this round:**
- an online run that arrives at a decided site differing from both the squad's current smart and stock's pick: none yet with the current adapter (F4's two such runs died); the mutant pack A/B pair is not shown;
- the probe's arrival rule: stock counts a squad as arrived when it shares the smart's game-graph vertex, which in the L3 pack B run left the leader 163.8 m from the smart, so the probe's rule (an `arrived` event and the leader within 150 m) reported a failure for an arrival stock accepted; the rule needs a decision;
- a held squad with a hold and no assigned smart is locked with no `blocked` event (about 16 in F3 `fix1`); the adapter header comment now says so;
- the Cordon→Darkscape offline loop;
- what remains of the blocked-stay loop (fixed in Task 8, below): a model that keeps choosing a blocked site costs one rejected call per reconsider interval (600 s by default) per such owner; the fix is unit-tested only, no `blocked` event occurred in the Task 8 live run;
- the 5 s adapter sync costs FrameMove on the main thread with the flag: temporary instrumentation gave a mean of 14–28 ms and a maximum of 25–58 ms per sync (`final-fix/frame-time-summary.txt`); the sighting pass is the largest part;
- whether autonomous wakes should share the queue with actor wakes (quit with a `claude_cli` request in flight was checked in Task 8: the game exits in about 3 s and no CLI process is left; the `anthropic` path was not run);
- live pace at Cordon scale: about 180 transferable owners (178 at the end of the Task 8 run) share the budget, so the first pass alone takes about 30 min at the `claude_cli` default of 6 per minute (178 / 6) and about 36 min at the 5 per minute of the run (178 / 5); in the 15-minute run 108 of 178 had no decision yet, and the 14.8 min maximum queue age is only the run's length. A reconsideration (every 600 s by default) queues behind every older wake, so in steady state each owner gets at most one decision per pass, about every 30 min at 6 per minute, later when event wakes or new squads join; a faster pace needs a higher budget (more subscription usage) or fewer wakes;
- decisions are handed out in queue order (oldest first) across all levels, so owners on the player's level are not served first (in the Task 8 run 26 of the 107 queued undecided owners were on the Cordon); a follow-up is planned;
- the per-minute submission count of the live run was not recorded with timestamps: only the average (75 replies in 900 s, 5.0 per minute at a cap of 5) and the unit-tested bound (`TestCallsPerMinuteCap`) are known;
- a level change is a load: objectives are lost on every level change until persistence exists.

**Persistence and enrolment:**
- objectives and owner identities are not saved: a load re-bootstraps every owner, and a travelling squad's decision is lost (F4);
- no saved activation setting: activation is the launch flag only;
- companion order persistence is untested: no companions exist in `cordon_start`;
- Sidorovich and the Forester are not enrolled (trader objects).

**Coverage:**
- the Cordon–Garbage crossing and Zone-wide checks beyond Cordon;
- autonomous model wakes with the `anthropic` provider (Task 7 steps 6–10), waiting on an API key; the `claude_cli` path has one 15-minute run (Task 8);
- the Linux fork/exec path of the `claude_cli` process runner is untested (only the macOS `posix_spawn` path ran); Windows has a stub (postponed by the user);
- `-keep_lua`: the double-wrap guard is only code-read and checked through `lua`; a `-keep_lua` session was not exercised;
- the Windows build of the new sources;
- `4f6044610` in power states other than locked with displays asleep.

**Known behaviour to watch:**
- respawned squads held at their spawn smart can leave a smart over its maximum population (F3: `8_10` and `8_9` at 3/2; F4's soak had none);
- during a provider outage, squads with no feasible smart stand still;
- stock "help the actor" (`get_help_target_id`) remains a second, stock destination writer.

**Deferred minor review items (ledger):**
- registry edge cases: dissolving group objectives, `:` in site names, linear owner lookups;
- prompt and line escaping, plus duplicated parse helpers that could collide in unity builds on other configurations;
- scheduler timeout and requeue paths without tests;
- in live mode a `decide` and a pending reply can both commit;
- probe robustness: silent fallbacks, untested branches;
- clone-tool hardening: symlinks, atomic writes, CRLF in `user.ltx`;
- the wrapper ignores `smart.locked` for stay/move targets;
- observation cost per wake;
- a resumed ex-companion objective gets no reset;
- sightings: a disarmed pair can stay disarmed forever if the other squad leaves the level and returns, and the actor changing level causes one burst of sighting wake requests;
- the squadless scan is now spread at 2048 IDs per tick; a whole pass at activation measured 74–78 ms (`final-fix/instr-timing.txt`);
- an in-flight live decision for a blocked site can re-apply it after the revert (untested);
- `ai.freeplay.mode off` on the engine does not stop `ai.freeplay.decide` or pending live replies from committing;
- the module-teardown join of the provider worker on quit.
