# Plan status — 2026-11-08

## Windows transfer — 2026-11-07

Current execution is moving to Windows. Read [WINDOWS_HANDOFF.md](WINDOWS_HANDOFF.md) before resuming: it records the reviewed menu repair, final Mac runtime results, WIP provenance/overhead branches, Windows bridge/export/build gaps and transfer recovery instructions. Historical checklist and benchmark claims remain subject to their original platform/evidence limits. Build, test, and live results below are macOS runs unless a 2026-11-08 section names a Windows command. The full engine has still not been built on Windows. The standalone `xrSimCore` tests in the Gate 1 recovery section are the exception.

## Repository and branch policy — 2026-11-07

- The repository is [rzgrw/opagxray](https://github.com/rzgrw/opagxray), now **detached** from OpenXRay's GitHub fork network. This supersedes the earlier "no GitHub detachment has occurred" wording here and in the handoff. OpenXRay and IXRay remain inspiration and selective donors: no PR flow upstream, upstream intake only as reviewed, attributed, license-compatible changes.
- Only two long-lived branches. `main` is stable (GitHub default; `950a7820b` on 2026-11-07) and moves only by fast-forward from `dev` after a recorded gate. `dev` is experimental integration. Feature work uses short-lived branches that are merged then deleted, or archived as annotated tags.
- Archive tags created by the 2026-11-07 cleanup (recover with `git fetch origin tag archive/<name>`, then `git switch -c <x> archive/<name>`): `archive/codex/ai-authority-spine-gate1` → `1d951aa07`; `archive/feat/metal-mr1-ingame` → `caa2e4953`; `archive/rzgrw/frame-overhead-base` → `617957470`; `archive/rzgrw/frame-overhead-reference` → `d603c01d2`; `archive/transfer/windows-2026-11-07` → `d336361d1` (only copy of the `docs/transfer/2026-11-07/` evidence and provenance WIP `8cf2af572`). `codex/ai-snapshot-replay` (patch on `dev` as `99ebfb8b5`), `codex/ai-zone-foundation` and `feat/metal-mr0-first-frame` (ancestors of `dev`) were deleted without tags. Tag `sp1-macos-baseline` is kept. None of the archived tips is a merge candidate.

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
| [Frame-time measurement](superpowers/plans/2026-11-07-frame-time-measurement.md) | Shipped infrastructure: recorder/reports `e3f273f04`, opt-in GL telemetry + `perf.*` bridge export `a389540d8`, readiness/visibility classification `456808010`, provenance sidecars `90c6e57e6`. No accepted capture or measured gain; Windows export unavailable. Overhead experiment is WIP in the `archive/rzgrw/frame-overhead-*` tags. |
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

Branch `integrate/gate1-recovery` (from `dev` `437cca1aa`) takes one self-contained piece and leaves the rest on the tags.

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
