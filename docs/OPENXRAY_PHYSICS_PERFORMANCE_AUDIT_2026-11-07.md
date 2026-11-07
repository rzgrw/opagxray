# OpenXRay physics audit — OPCODE 1.3 and the Jolt series

**Date:** 2026-11-07
**Local tree:** `/Users/rz/orca/opagxray` `dev` `11cd585987477bdf2092fdf9b938b4b7c114a488`
**Upstream:** [OpenXRay/xray-16](https://github.com/OpenXRay/xray-16), read-only `gh`. Provenance: [artifacts/openxray-physics-audit/provenance.md](../artifacts/openxray-physics-audit/provenance.md).
**No build, game, or benchmark was run.** Every millisecond below is an author claim.

## Decision

Do not adopt [#2164](https://github.com/OpenXRay/xray-16/pull/2164)–[#2168](https://github.com/OpenXRay/xray-16/pull/2168) or [#2169](https://github.com/OpenXRay/xray-16/pull/2169) now, on the evidence available. Do not fetch Jolt, and do not replace OPCODE 1.2, as part of this decision. None of these PRs is a small patch this tree can validate and take today. This is the current decision, not a permanent prohibition. The project keeps its own roadmap and release authority, still tracks OpenXRay, and can take a later patch when it fits. A future measured architectural experiment can own an external library, including Jolt or OPCODE 1.3, when the evidence and an explicit decision exist.

There is no salvageable measured speedup to port. Two portable correctness candidates are listed below, and they are not performance proof: capture the `-mt_cdb` lambda arguments by value in `MODEL::build`, and reject truncated collision caches while keeping the current cache names.

The author's own tables already fail the reason to switch. On the scenes they published, native Jolt's whole frame stays slower than ODE, and OPCODE 1.3's combat FPS is mixed while its physics step is slightly slower. Those runs are Windows x64, Direct3D R2, Call of Pripyat and Clear Sky. They are not this tree, not OpenGL, not Apple Silicon, and not CoC 1.4.12 + 1.4.22.

No remote, branding, or license change is part of this audit. If a later experiment copies a file, keep Pierre Terdiman's OPCODE notice, the OGSR snapshot id, and, only if Jolt is ever built, Jolt's license and the unxray attribution the author already recorded.

## Maintenance

| Piece | Footprint we would own | Upstream coupling | Can we validate it here |
|---|---|---|---|
| OPCODE 1.2 + X-Ray ODE, current tree | Already in tree. OPCODE ReadMe is distribution 1.2 (August 2002), 68 files. ODE `ode.cpp` is Russell Smith 2001–2002, LGPL 2.1 or BSD, `dWorldStep` / `dWorldQuickStep`, ERP/CFM | Frozen snapshot. No live physics upstream required | Current playable GL path. Save layout and Lua stay as they are |
| OPCODE 1.3, #2169 | PR total **+27835 / −456, 140 files**. About 101 of those are a second vendor tree, `Externals/OPCODE13`, from OGSR `01faa139995f` | Dead 2003 library via an OGSR snapshot. After the copy, nobody else maintains it for us. The adapter is the small part; the behavior is the vendor tree | Author's Windows tests only. `std::make_unique` in `ModelTree.cpp` does not match `xr_alloc`. Darwin `XRAY_EXCEPTIONS=0` still has to compile that path. No CoC/GL run exists |
| Jolt queries, prerequisite [#2149](https://github.com/OpenXRay/xray-16/pull/2149) | **+3297 / −45, 34 files**, plus the downloaded Jolt repo | `FetchContent` of [jrouwe/JoltPhysics](https://github.com/jrouwe/JoltPhysics) `e77f175595e64cb44218cc9d9d56fc365ad0e36a` ("Bump version to 5.6.0", 2026-07-11). First configure needs that host unless `FETCHCONTENT_SOURCE_DIR_JOLT` is set | Author reports macOS ARM64 CTest for this query switch only. Later gameplay numbers do not show a clear query win over OPCODE |
| Jolt bridge, #2164 | Layer is 18 files, **+1171 / −18**, still linking ODE | ODE and Jolt together | Author: idle physics slower. Linux/macOS bridge not run |
| Native Jolt, #2166–#2168 | Cumulative PR at #2168 is **+20381 / −17101, 265 files**. The #2166 layer names 197 paths: new `src/xrPhysicsCore` (`JoltPhysicsCore`, character/contact/joint services) and deletion of `src/xrPhysics/tri-colliderknoopc` plus ODE include shims. GitHub's compare line counts are blank for 80 of those 197 paths | Same fetched Jolt 5.6.0, plus a fork-specific physics ABI. Game and physics binaries must ship together. xrCDB remains a wrapper | Author says campaign, vehicles, multiplayer, Linux, and Debug are uncovered. Shutdown `0xC0000005` is unexplained. Reload check allows 1 m of physical-root drift |

Jolt's own `Build/CMakeLists.txt` at that pin defaults `CPP_EXCEPTIONS_ENABLED` to OFF and adds `-fno-exceptions`. The engine wrapper does not turn exceptions on. That matches Darwin's rule on paper. `DISABLE_CUSTOM_ALLOCATOR` is forced ON, so Jolt does not use the engine allocator. `FLOATING_POINT_EXCEPTIONS_ENABLED` defaults ON, and Jolt says Clang ignores it. None of this was configured on this machine.

A wholesale solver replacement stays deferred until a measurement on this GL runtime exists. Even then it is an explicit future decision. Speed alone does not adopt it, and a later pin is not barred because Jolt is third-party. Owning that experiment would mean supporting Jolt's job system, its allocator, its contact/sleep model, and a rewritten `xrPhysics`, without ODE's ERP/CFM. Isolated mechanisms worth a second look are listed under "Portable pieces". They are correctness candidates, not an adoption and not a speedup.

## Inspected heads

All six are open and unmerged. Jolt PRs are GitHub drafts. #2169 is **not** a GitHub draft (`isDraft: false`); the PR text still calls the work a draft. No review was submitted on any of the six. #2168 has three issue comments: one non-technical note from `klaxons1`, and two follow-ups from the author. Base of #2164–#2169 is `dev` `d4979c2bc496be471ee3ff2b399df18827d2078e`. #2149's base is the older `94fea9197130f5e59106e856a25b60721aeea61f`.

| PR | Head | Role |
|---|---|---|
| [#2149](https://github.com/OpenXRay/xray-16/pull/2149) | `650f204d459d40789b9c8f077b5b6b1d5b5e5449` | Prerequisite, OPEN GitHub draft. Optional `XRAY_USE_JOLT_CDB`. Jolt 5.6 pin `e77f175595e64cb44218cc9d9d56fc365ad0e36a`. OPCODE stays the default. Not one of the six, but every Jolt PR contains it |
| [#2164](https://github.com/OpenXRay/xray-16/pull/2164) | `e10c530d25cb2146e278abd8499fd2cc0600bcb8` | Opt-in dynamics bridge. ODE remains the world. Layer review range `650f204d4...e10c530d2` |
| [#2165](https://github.com/OpenXRay/xray-16/pull/2165) | `888ab2d02b9c906c84b04bbe17d12765c0cf884c` | Benchmark harness and the first CoP numbers. Layer `e10c530d2...888ab2d02` |
| [#2166](https://github.com/OpenXRay/xray-16/pull/2166) | `3fb3ff5013484126c3af40c52b64a59856d512c6` | Native bodies, contacts, characters, joints. Adapted from [unxray `c3e09c9`](https://github.com/xxcactussell/unxray/tree/c3e09c9f1c5c7b1f4674aa460181bafc9e755eb2). Layer `888ab2d02...3fb3ff501` |
| [#2167](https://github.com/OpenXRay/xray-16/pull/2167) | `25ad9bb2e2fb72359b0a5a6de362f0234e46c1d4` | Contact-prep and query cuts. Measured code is `c49e12412`; the tip commit publishes tables. Layer `3fb3ff501...25ad9bb2e` |
| [#2168](https://github.com/OpenXRay/xray-16/pull/2168) | `774f58452cfb1e26847397f4b24f272d999f2509` | Dry regions, passive joints, coherent body reads. Measured tips `82307c47d` then `a2d963eef`. Layer `25ad9bb2e...774f58452` |
| [#2169](https://github.com/OpenXRay/xray-16/pull/2169) | `fcf7892901f3b1b62187acfa984061f6f0af2468` | OPCODE 1.3, no Jolt commits. `1757c652a0f8` is the library drop; `fcf7892901f3` removes the Apple `bool` macros and drops a redundant `FS.exist` on two cache loads |

GitHub PR files for the Jolt series are cumulative because each draft targets `dev` while the parent branch lives only on `makarkotlov/xray-16`. The layer ranges above are the author's review boundaries and match the commit list.

## Local tree versus that base

These blobs match OpenXRay `d4979c2` exactly: `src/xrCDB/xrCDB.cpp`, `xrCDB.h`, `xrCDB_ray.cpp`, `xrCDB_box.cpp`, `xrCDB_frustum.cpp`, `xr_area.cpp`, `src/xrPhysics/PHWorld.cpp`, `PHIsland.cpp`, `Physics.cpp`, `PHActorCharacter.cpp`, `PHSimpleCharacter.cpp`, `Externals/OPCODE/ReadMe.txt`, `OPC_AABBCollider.cpp`, `Externals/ode/include/ode/ode.h`.

There is no `Externals/Jolt-proj`, no `OPCODE13`, and no `XRAY_USE_JOLT` string in product code. The collision and dynamics implementations in this list are the PR's starting point. They are not an earlier port of either series.

`Externals/OPCODE/CMakeLists.txt` differs by one include root (`CMAKE_SOURCE_DIR` instead of `XRAY_SOURCE_DIR`), from local `9352fe4bb`. #2169 also rewrites that CMake file and the Visual Studio project so the old `Externals/OPCODE` path compiles 1.3. That hunk does not apply as a pure vendor add.

Current integration, unchanged by this audit:

- Static world: `CObjectSpace` loads `level.cform` and a side cache (`src/xrCDB/xr_area.cpp`). Queries are `CDB::COLLIDER::ray_query` / `box_query` / `frustum_query`.
- Build copies vertices, packs a temporary index array, then `OPCODE_Model::Build` with `SPLIT_COMPLETE | SPLIT_SPLATTERPOINTS | SPLIT_GEOMCENTER`, no-leaf, not quantized (`src/xrCDB/xrCDB.cpp`).
- Dynamics: `CPHIsland::Step` calls `dWorldStep` or `dWorldQuickStep` (`src/xrPhysics/PHIsland.cpp`). Triangle meshes enter ODE through `dCreateTriList` (`PHWorld.cpp`, `tri-colliderknoopc/`).
- Default step is 0.01 s and `phIterations` is 18 (`src/xrPhysics/Physics.cpp`). The author's benches force `ph_frequency 100` and the same 18 iterations.
- Call sites that matter for a later gate: bullets `src/xrGame/Level_bullet_manager_firetrace.cpp`, stalker cover rays `src/xrGame/stalker_movement_manager_smart_cover.h`, camera `src/xrGame/CameraLook.cpp` and `src/xrPhysics/ActorCameraCollision.cpp`, actor/AI bodies `PHActorCharacter.cpp` / `PHAICharacter.cpp` / `PHSimpleCharacter.cpp`.
- Cache open plus `deserialize`: `src/xrCDB/xr_area.cpp`, `src/Layers/xrRender/HOM.cpp`, `src/Layers/xrRender_R2/r2_loader.cpp`.
- `-mt_cdb` builds on a worker (`xrCDB.cpp`). The engine flag the benches turn off is `mt_physics`.

`PLAN_STATUS.md` and `AGENTS.md` keep the priority on OpenGL frame-time consistency. Native renderer work stays paused. Darwin keeps `XRAY_EXCEPTIONS=0`.

## What the benches actually timed

Common author conditions, from the PR bodies and `misc/windows/benchmark-gameplay.ps1` at `774f58452`: Windows x64, MSVC 19.44, Ryzen 9 9955HX, RTX 5070 Ti Laptop, **R2 at 800x600**, vsync off, `rs_fps_limit 501`, `mt_physics off`, 100 Hz physics, 18 iterations, three processes, order reversed on the second pass, no compile during the timed run. Engine binary hash must match across backends; physics/CDB DLLs may differ. Assets and raw logs stayed on the author's machine. Published artifacts are scripts, compact CSV/JSON, and one symbolized stack.

That is a controlled in-process fixture, not a campaign and not free play.

- **Synthetic** (`doc/design/jolt_collision.txt`, #2149): 32,768 terrain triangles, 20,000 queries, seed 2139. No game.
- **Scripted idle + ragdolls** (`gameplay_benchmark.script` at `888ab2d02`): 10 s warmup, 10 s idle, spawn 12 `sim_default_stalker_0`, kill them with scripted hits, then `benchmark_force_shell` for 20 s. The actor does not play.
- **Scripted combat** (#2169 and the #2168 comment): one prepared save, 50 NPCs, two groups of 25, AK-74s. A stationary actor fires through normal input and does not use the NPC planner. CoP near (256, 16, 510) in Zaton; Clear Sky near (240, 3, 70) in the Marshes. Casualties differ between runs.
- **Static probes** (`opcode_queries.script`): after 10 s, 4,096 fixed-seed queries from a literal origin. The script says it is not a combat or physics-step workload.
- **Stack/chain** (#2166): 128/512/2048 boxes, no gameplay callbacks. This is the workload where four Jolt workers beat ODE. It does not include the adapter that dominates the game profiles.

`mt_physics off` disables the engine's physics thread. Native runs still start a Jolt pool of 0, 1, or 4 workers. A 4-worker wall-time drop can raise CPU and RSS. The author says so.

OPCODE 1.3 timings used a frozen tree at `4c27925c4`, with 1.2 and 1.3 sharing engine/game/physics binaries. The tip `fcf78929` is a later macOS/cache edit and was not the timed binary. #2169's component-build note also says the 1.2 library in the synthetic builder timing was the native Release build and 1.3 was CMake, so that one builder column mixes library version with build profile.

## OPCODE 1.3 mechanism

Source of the vendor drop, from `Externals/OPCODE13/Integration.md` at `fcf78929`: OGSR/OGSR-Engine `01faa139995f6afdc66addb48065f053d38f397e`, directory `ogsr_engine/xrCDB/OpCoDe`, 98 blobs checked. The ReadMe in that tree is "OPCODE distribution 1.3 (june 2003)". The solbjorn repository named in #877 / #2139 was unavailable. Notices stay in `Opcode.h` (Pierre Terdiman, 2001).

`ModelTree::Build` (new, 102 lines in `src/xrCDB/ModelTree.cpp`):

- Owns an `Opcode::MeshInterface` with 16-byte `TRI` and 12-byte `Fvector` strides, `mCanRemap = false`. The temporary packed index array in `MODEL::build_internal` goes away.
- `mNoLeaf = true`, `mQuantized = false`, `mLimit = 1`, rules `SPLIT_SPLATTER_POINTS | SPLIT_GEOM_CENTER`.
- 1.3's ReadMe says `SPLIT_COMPLETE` was removed and is now implied by `mLimit = 1`. Local 1.2 still passes `SPLIT_COMPLETE` because `OPC_Model.h` calls that flag mandatory. The new builder is the 1.3 replacement, and it can still shape a different tree than 1.2.
- 1.3's ReadMe also lists linearized complete AABB trees, strided meshes, and volume queries that can skip primitive-versus-bounds tests.

X-Ray's walkers stay in `xrCDB_ray.cpp`, `xrCDB_box.cpp`, and `xrCDB_frustum.cpp`. Each of those diffs is a leaf-accessor rename (`HasLeaf` / `GetPrimitive` to `HasPosLeaf` / `GetPosPrimitive`) plus, in the box file, `using IceMaths::Point`. The box speedup is therefore the 1.3 tree and the headers those walkers include, not a new query policy. `xrCDB.cpp` is **+42 / −56**: construct `ModelTree`, capture the worker lambda's pointer arguments by value, append `.opcode13` to cache names, and reject short or corrupt caches. A failed `Build` becomes `R_ASSERT2` (fatal while exceptions are off). Today a failed build frees the buffers and returns.

`deserialize_tree` still accepts an embedded 1.2 no-leaf tree when the header says no-leaf, not quantized, and the node count is `triangleCount - 1`, then checks finite bounds, leaf ids, and in-order child offsets. Failure falls back to `Build` from the vertices already loaded. Old app-data caches are left in place and not read, because the suffix changed. Retail `level.cform` geometry is unchanged. Generated caches are not part of the CoC archives.

The second commit deletes the Apple block in `Ice/IceTypes.h` that `#define`d `bool` as `char`. That is a compile fix. It also removes `FS.exist` before `deserialize` in `HOM.cpp` and `r2_loader.cpp` so the new suffix can be opened. Those two lines are cache lookup, not a renderer change. `xr_area.cpp` has the same one-line edit.

### Author numbers for 1.3 (claims)

| Workload | Result they published | How to read it |
|---|---|---|
| Loaded-level full box | CoP 0.502 to 0.368 µs (−26.7%); Clear Sky 4.946 to 3.200 µs (−35.3%) | Fixed probes, hit counts and nearest-distance sums matched. Not the solver |
| Loaded-level rays | CoP nearest/any −3.6% / −5.3%; Clear Sky −16.7% / −15.8% | Same probes |
| Synthetic 20,000 queries | Boxes 44.371 to 28.515 ms (−35.7%); rays 4.250 vs 4.253 ms; build 8.539 to 10.309 ms (+20.7%); warm cache 5.052 to 5.567 ms | Builder column confounded by native vs CMake. Rays flat |
| Combat, first 10 s, median of 3 | CoP FPS 134.3 to 133.7 (−0.4%), physics 0.250 to 0.271 ms. Clear Sky FPS 73.4 to 83.1 (+13.2%), physics 0.231 to 0.245 ms | Physics step slower in both. FPS ranges overlap or the scene diverges |
| Combat, full 30 s | CoP 258.0 to 279.1 FPS (+8.2%); Clear Sky 203.0 to 195.3 (−3.8%) | Includes the quiet aftermath. Casualties differ |

Two extra CoP combat runs completed timing and then crashed after `KERNEL:QUIT` with `0xC0000005`. They were excluded. A small-dump rerun exited cleanly and produced no stack. The author says the cause is open.

Local evidence for all of the above: none.

## Jolt series mechanism

**#2149** keeps ODE. It can swap the static-query tree to Jolt's `AABBTreeBuilder` / `TriangleSplitterBinning` under `XRAY_USE_JOLT_CDB`. Rays still use the CDB triangle test. The design doc says ONLYFIRST identity and result order may differ. Caches use a `.jolt` suffix and rebuild the tree on load, so a cache hit still pays construction. AVX/SSE knobs in the wrapper are all OFF. Interprocedural optimization is OFF.

Author synthetic claim at this pin: versus their OPCODE, Jolt build was faster and model memory lower, nearest rays about 45% slower, boxes about 65% slower. Same fixture versus unxray's collision subset: unxray was slower and its box sets disagreed. That comparison is Windows.

**#2164** adds `XRAY_USE_JOLT_PHYSICS` as a bridge: Jolt integrates, contacts and joints are translated, ODE handles and gameplay state remain. `JoltDynamicsWorld.cpp` is 461 lines in the layer. The author says ERP/CFM, friction, motors, and island ownership are preserved only by translation, and the bridge is not the unified world from #2139.

Author claim with OPCODE held constant: idle complete step **0.0611 ms ODE vs 0.1076 ms bridge**. Driven ragdolls **0.819 vs 0.808 ms**, overlapping ranges, different contact counts. No FPS case for the bridge.

**#2165** is the harness above plus this table (medians of process means, ms):

| Collision / dynamics | Idle | 12 driven ragdolls |
|---|---:|---:|
| OPCODE / ODE | 0.0611 | 0.819 |
| Jolt CDB / ODE | 0.0593 | 0.618 |
| OPCODE / bridge | 0.1076 | 0.808 |
| Jolt CDB / bridge | 0.1007 | 0.805 |

The ragdoll cell for Jolt CDB + ODE is a different contact workload, which the author refuses to call a pure query win. On matched real-level batches with ODE fixed: nearest ray 0.663 vs 0.822 µs, any ray 0.443 vs 0.506, full box **1.588 vs 0.931 µs (box regression 70.5%)**. Reported model bytes 118,187,388 to 111,372,664. That is a model estimate, not process RSS.

**#2166** deletes the ODE triangle collider and drives native Jolt bodies off one shared mesh BVH. Lua and the save/network encoding stay; the C++ physics ABI does not. Author initial claim, four workers: idle **0.860 vs parent 0.060 ms (about 14×)** and ragdolls **1.755 vs 0.869 ms**. Contact preparation is about 61–63% of the native step. Shared-mesh queries are slower than the parent CDB path. Synthetic stacks are 4.23–7.19× faster and chains 1.54–3.07× at four workers; small single-thread chains still favor ODE. Accepted reloads use a **1 m** root tolerance. An ODE reload miss at 0.5 m was excluded.

**#2167** caches motor settings, defers ordinary impact effects, reuses contact buffers, and adds SIMD ray/box child tests inside the Jolt query path. Author claim, four workers versus ODE: idle **0.367 vs 0.063 ms (5.83×)**, ragdolls **0.777 vs 0.611 ms (1.27×)**. One worker slightly beat four workers on ragdolls. Frame p95 improved versus the first native port and remained worse than ODE (ODE 3.406 / 4.118 ms idle/ragdolls, optimized native 3.843 / 4.563). One optimized process returned `0xC0000005` after quit and was replaced. The author says replacements do not establish a fix.

**#2168** is adapter work on top of an unchanged Jolt kernel: dry-region cache, passive joint friction instead of `ActivateBody` on zero-speed resistance, batched body reads, coarser fluid rejection, one coherent state read, effect eligibility before material lookup. The diagnostic idle fixture counted 342,242 motor activation calls per 100 steps before the change. Final repeated claim (`a2d963eef`):

| | Idle ms | Ragdoll ms | Ragdoll range | Mean frame ms | Frame p95 ms |
|---|---:|---:|---|---:|---:|
| Clear Sky ODE | 0.03338 | 0.68422 | 0.671–0.687 | 1.7173 | 2.7736 |
| Clear Sky native, 4 workers | 0.14750 | 0.96643 | 0.935–1.038 | 2.2227 | 3.5876 |
| CoP ODE | 0.06395 | 0.70598 | 0.593–0.736 | 3.1174 | 4.2229 |
| CoP native, 4 workers | 0.09906 | 0.64529 | 0.611–0.674 | 3.2476 | 4.3660 |

CoP's 8.6% lower ragdoll median overlaps ODE's range. Clear Sky's ragdoll ranges do not overlap, and native is slower. An earlier Clear Sky near-tie did not survive the repeat. Whole-frame columns favor ODE in both games. Contact preparation on an instrumented Clear Sky capture stayed about 0.446 ms. A diagnostic reload crashed in `ntdll!RtlpCoalesceFreeBlocks` during `D3DCompiler_47` shutdown. That locates the detector on Windows, not the write, and it does not clear the migration.

#2139 stays open on the author's own terms: xrCDB is still a DLL, and the gameplay premise is not shown for these two games.

## Portable pieces

No salvageable speedup comes out of this list. The first two rows are portable correctness candidates, not performance proof: lambda argument capture, and collision-cache validation that rejects truncated caches and keeps the current cache names. The remaining rows are not portable speedups. None of these pieces needs Jolt, and none reproduces the published query times, because those times sit inside OPCODE 1.3's builder and headers or inside Jolt.

| Piece | Where | Worth a separate change? |
|---|---|---|
| Capture `-mt_cdb` lambda arguments by value | `MODEL::build` in #2169's `xrCDB.cpp` | Only if we ship `-mt_cdb`. The copy into `verts`/`tris` already happens before `status` leaves `S_INIT`. `status` is a `volatile u32`, not an atomic release. The capture fix is real and small; it is not a frame-time change |
| Reject truncated collision caches | same file, and `ModelTree::Load` | The checks are sound. Porting them onto the 1.2 loader is a different, smaller patch than vendoring 1.3. Do it only as a robustness fix, with the current cache names left intact |
| Leaf-accessor rename and `using IceMaths::Point` | the three walker files | Useless without the 1.3 headers |
| `FS.exist` removal | `xr_area.cpp`, `HOM.cpp`, `r2_loader.cpp` | Required only by the `.opcode13` suffix. Leave the calls as they are |
| Apple `bool` macro deletion | `OPCODE13/Ice/IceTypes.h` | Applies only to that vendor drop |
| Jolt dry-region, passive-joint, and contact-prep cuts | `xrPhysicsCore` at #2168 | They assume the native world. There is no ODE-shaped equivalent that drops out as a few functions |

`R_ASSERT2` on bad collision input is a behavior change under `XRAY_EXCEPTIONS=0`: bad geometry aborts instead of returning. Do not take that with the cache checks.

## Bounded experiment

Run this only to decide whether static box queries can matter on our GL frame. Do not treat a pass as permission to merge #2169 or to start Jolt.

**Step 1, no upstream code.** On the current binary, CoC 1.4.12 + 1.4.22 and the target GL overlay, one new Cordon game, vsync off, private appdata:

- Time `CPHWorld::OnFrame` / `CPHWorld::Step` (`src/xrPhysics/PHWorld.cpp`).
- Read the counters already in `src/xrCDB/xrXRC.h`: `RayQuery`, `BoxQuery`, `FrustumQuery`.
- Record process RSS and frame wall time as p50/p95/p99 after warmup. When this audit was first scoped, the frame-time collector was still uncommitted. The reviewed standalone checkpoint is now `e3f273f04bf85b4b265004cbbc8a35f19796cd31`, and engine hooks are in progress. A same-binary steady clock only attributes intervals inside one binary. It does not close the overhead gate in [the measurement plan](superpowers/plans/2026-11-07-frame-time-measurement.md) or [the design spec](superpowers/specs/2026-11-07-frame-time-measurement-design.md). That gate stays open until a common external or separately gated reference can measure disabled and pre-change binaries. Do not use bridge FPS or the author's `calc_ms`.
- Scenes: idle at a fixed camera, and one scripted firefight. Three launches. Keep binary hashes.

Stop if box-query time is a small slice of the frame. #2169's own physics-step column already got slightly worse when boxes got faster, which is what a small slice predicts. In that case the library drop cannot pay for itself here.

**Step 2, only if step 1 says box queries dominate.** Side-by-side build. Keep today's `Externals/OPCODE` 1.2 as the default target. Add `Externals/OPCODE13` from `fcf78929` with `Integration.md` and the Terdiman headers untouched. Take `src/xrCDB/ModelTree.cpp`, `ModelTree.h`, and the `xrCDB.cpp` / walker / `stdafx.h` / `CMakeLists.txt` edits needed to reach `ModelTree`. Include the three `FS.exist` call sites only together with the suffix. Do not gut the 1.2 CMake file the way #2169 does, and do not take the HOM/portals edit as renderer work beyond the cache filename. Allocator adaptation is an assessment, not a required rewrite: do not replace `std::make_unique` with `xr_alloc` without a lifetime and ownership analysis. Leave `R_ASSERT2` as a logged rebuild failure so exceptions stay off.

Correctness gates, same Cordon save, both binaries:

- Collision: nearest-ray distance, material, and geometry result, full-box triangle-id set, and one frustum, against 1.2. ONLYFIRST may return a different triangle id at the same distance. A legitimate equal-distance tie, same geometry and material result with a different triangle id, passes. A different distance, material, or sector fails.
- AI rays: one stalker cover/LOS ray and one bullet trace (`Level_bullet_manager_firetrace.cpp`) keep material and sector.
- Character: a scripted walk, jump, and crouch, plus camera collision, with no fall-through.
- Contacts and save/reload: actor root and one settled prop after save and reload. Tolerance is centimeters. The native Jolt 1 m allowance does not apply. Embedded `level.cform` loads. A cache without `.opcode13` is ignored. `-mt_cdb` and the synchronous build both finish. Process exit code is 0.
- Frame and RSS: same method as step 1. A box-query drop that does not move p95, or that raises RSS or load time past the step-1 baseline, ends the trial.

Jolt is not step 3 of this trial. Do not adopt #2164–#2168 now. A future measurement on this GL runtime would still have to show ODE `CPHWorld::Step` itself dominating p95 after the collision tree is already cheap. Even then adoption is an explicit decision, and that measured architectural experiment can own the external library. This audit does not adopt it now.

## Coverage limits

- Read the six PR bodies, commit lists, file lists, #2149's design note and Jolt `CMakeLists.txt`, #2169's `Integration.md`, `ModelTree.cpp`, walker patches, both benchmark scripts, and the Jolt 5.6.0 exception options. Did not read all 19k vendor lines or the fetched Jolt sources.
- GitHub compare under-reports additions on large patches. #2166's "+4863 / −8565" is incomplete (80 paths came back as +0 / −0, including new `xrPhysicsCore` files and the deleted triangle collider). Cumulative counts quoted from `gh pr view` are the ones used for PR size. Path names are the reliable inventory.
- No independent reviewer examined these PRs. The author wrote the result tables.
- No local timing, cache, save, or shutdown check was run. A clean reading of the Windows D3DCompiler stack says nothing about GL on Darwin, and it does not clear the native heap failure either.
- The sibling survey [docs/OPENXRAY_PERFORMANCE_AUDIT_2026-11-07.md](OPENXRAY_PERFORMANCE_AUDIT_2026-11-07.md) links here for the physics decision. This file does not reopen renderer or NVRHI PRs. Provenance remains [artifacts/openxray-physics-audit/provenance.md](../artifacts/openxray-physics-audit/provenance.md). No local benchmark was run.
