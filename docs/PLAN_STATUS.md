# Plan status — 2026-11-07

## Windows transfer — 2026-11-07

Current execution is moving to Windows. Read [WINDOWS_HANDOFF.md](WINDOWS_HANDOFF.md) before resuming: it records the reviewed menu repair, final Mac runtime results, WIP provenance/overhead branches, Windows bridge/export/build gaps and transfer recovery instructions. Historical checklist and benchmark claims remain subject to their original platform/evidence limits. All build/test/live results recorded below are macOS runs; none has been repeated on Windows.

## Repository and branch policy — 2026-11-07

- The repository is [rzgrw/opagxray](https://github.com/rzgrw/opagxray), now **detached** from OpenXRay's GitHub fork network. This supersedes the earlier "no GitHub detachment has occurred" wording here and in the handoff. OpenXRay and IXRay remain inspiration and selective donors: no PR flow upstream, upstream intake only as reviewed, attributed, license-compatible changes.
- Only two long-lived branches: `main` (stable; `<main-sha TBD>`, not yet created) and `dev` (experimental; all work lands here first). Other branches (`codex/*`, `feat/metal-*`, `rzgrw/frame-overhead-*`, `transfer/windows-2026-11-07`) are being archived/removed; SHAs cited below for them are historical recovery references, not merge candidates.

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
| [Frame-time measurement](superpowers/plans/2026-11-07-frame-time-measurement.md) | Shipped infrastructure: recorder/reports `e3f273f04`, opt-in GL telemetry + `perf.*` bridge export `a389540d8`, readiness/visibility classification `456808010`, provenance sidecars `90c6e57e6`. No accepted capture or measured gain; Windows export unavailable. Overhead experiment is WIP outside `dev`. |
| Menu reload repair | Shipped: `604fb0690` (failure ownership, activation-effect restore, `xrMainMenuReloadTests`). Recorded macOS focused/sanitizer tests and two private live saved-load lifecycles; root cause of the wrong factory type unknown; Windows unverified. |
| [Metal SP2](superpowers/plans/2026-03-29-sp2-metal-renderer.md) | M-R0/menu shipped (`1ed42b1f1`, merge `4dba4ecd1`); M-R1 parked at unmerged `caa2e4953`. Resume details in [HANDOVER](HANDOVER.md). |
| [Vulkan](superpowers/specs/2026-07-05-vulkan-renderer-roadmap.md) | Not started; native-renderer work paused. Metal on Mac / Vulkan on PC remains the long-term direction. |
| [Gate 1 authority spine](superpowers/plans/2026-07-14-garbage-ai-authority-spine.md) | Task 1 selectively recovered and integrated at `04b4ef5b5` after independent review. Tasks 2 onward are not integrated; further AI expansion follows the runtime comparison. |
| [Old roadmap](superpowers/specs/2026-07-04-engine-v2-roadmap.md), [deterministic Zone](superpowers/specs/2026-07-05-zone-simulation-design.md), [streaming](superpowers/specs/2026-07-05-seamless-zone-streaming-design.md), [continuation](superpowers/CONTINUATION.md) | Superseded or parked; retain historical research and surviving invariants, not their execution order. |

## Gate 1 recovery and remaining gates

`origin/codex/ai-authority-spine-gate1` locally resolves to `1d951aa07` and is
**unmerged** (the branch is being archived under the branch policy; recover by SHA). It contains partial Tasks 1–10 from `facec9eb9` / `3cf0a8543` plus
the standalone test seam at `1d951aa07`. This is recovery material, not a branch
ready to merge or proof that all tasks are executable as written. The separate
`f1222d148` reanimation document's Gemini/model/version-ladder changes are
**PROPOSED, not approved**; they do not amend the current provider contract.

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

## Frame-time measurement and upstream intake

The standalone recorder/report checkpoint is `e3f273f04`. Runtime loop and GL CPU spans, main-thread bridge controls, and exclusive stopped-only export have independently reviewed implementation and a full GL build plus private Cordon functional run. Coordinator reran three CMake test executables and 64 Python tests, with reviewed source/runtime hashes matched. This is measurement infrastructure, not a measured speedup. The current capture intentionally fails acceptance for unknown content/overlay provenance; classification edge cases, an independent overhead reference, repeated performance captures and equivalent donor timing remain open. See the [measurement plan](superpowers/plans/2026-11-07-frame-time-measurement.md).

The [OpenXRay adoption audit](OPENXRAY_PERFORMANCE_AUDIT_2026-11-07.md) and [physics audit](OPENXRAY_PHYSICS_PERFORMANCE_AUDIT_2026-11-07.md) rank selective candidates. Grass shadow culling is a performance experiment; shared-string ownership and smaller GL/game guards are correctness candidates. OPCODE/Jolt replacements are deferred on current evidence. Independence means our own roadmap and release authority while selective future upstream intake remains possible. The GitHub fork relationship has since been detached (repository `rzgrw/opagxray`); no license change has occurred.

### Classification follow-up

Not-ready intervals now use their executed phase rather than a device-loss flag; visibility unavailable/unknown is observed before early-return paths and excluded from steady analysis. Independent Grok review approved the delta. Full GL build, three CMake tests, 67 tooling tests and sanitizers passed in the author run; the coordinator matched source/binary hashes and reran three CMake tests plus 26 focused tests. A fresh sibling private Cordon runtime passed movement/save/reload/inventory/menu/normal quit and PID-targeted hide/restore during loading and gameplay. Six diagnostic captures have no invalid/dropped loops, but still fail asset provenance acceptance. Forced not-ready, indefinite-load, independent device-loss/reset, unknown-visibility API failure and Cocoa-only occlusion remain unexercised live. See `artifacts/frame-time/task-b0c500502419/report.json`; no performance gain is inferred.

### Provenance checkpoint

The standalone seal/attest sidecar is independently reviewed; 40 focused tests, 16 independent probes and a coordinator rerun of all 107 tooling tests pass. It preserves raw capture bytes and separates asset verification from full measurement acceptance. Live runtime/image observation and the overhead-specific adapter remain open, so no historical capture becomes accepted and no performance number is claimed.
