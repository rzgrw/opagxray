# IX-Ray foundation adoption — milestone 1 implementation plan

> Execution is already authorized: assigned workers implement their own scope; the coordinator owns integration and independent review. Leave changes uncommitted.

**Goal:** Stage a private, reproducible CoC runtime and correct the two confirmed target defects on `dev` baseline `a4d0fa256`.

**Architecture:** Keep the existing GL engine and data formats. Apply local corrections at the unsafe member dereferences and shader-cache key comparison; use the actual production comparator in regression tests, without introducing test-only production abstractions.

**Tech stack:** C++20 (the current `cmake/XRay.Build.cmake` setting), existing CMake targets, Python runtime staging/tests, agent bridge.

**Authority:** Authorized milestone in the dispatch, `AGENTS.md`, and `docs/HANDOVER.md`; later renderer and AI roadmaps remain outside this milestone.

## Constraints and provenance

- Preserve GL, AI bridge/xrSim, Darwin `XRAY_EXCEPTIONS=0`, formats, and nonempty group behavior.
- No thumbnails, renderer migration, xrRHI, or new audio imports; donor projects are read-only.
- Donor evidence: `ixray-1.6-stcop` commit `bcdce713b0c4e796347b7df242f30d75b897b81e` (dami.sirbu, empty group checks) and `43f15a022d87e4606ae40b0c2b419dfd5c3da69a` (Radik Zagirov, shader key ordering). Reimplement the minimal behavior in target style, retain existing source notices, and copy no donor files wholesale.

## 1. Target corrections and focused tests — foundation worker

Files: `src/xrGame/alife_online_offline_group.cpp`, `src/xrUICore/XML/UITextureMaster.h`, focused test sources and minimal test build wiring.

- [x] Add a test that includes the production `sh_pair`, checking ordering laws, equal-texture shader ordering, distinct crossed keys, and cache insertion/lookup across insertion orders.
- [x] Run against the original header and record the expected ordering failure; correct the comparator so shader order only breaks a texture tie; rerun to green.
- [x] Guard online commander access in both `update()` and `synchronize_location()` with nonempty membership, preserving the latter's `true` result and all existing nonempty/offline paths.
- [x] Assess actual group-method testing: no isolated engine fixture exists here; direct online-empty-group execution remains unverified. Do not substitute copied bodies or source-grep assertions.
- [x] Check the scoped diff and record exact test commands/results for review.

### Foundation verification record

- `src/xrUICore/tests/UIShaderCacheTests.cpp` uses the production header, interned strings, pool, and map; it suppresses unrelated Lua registration only. Minimal build wiring adds `xrUIShaderCacheTests` (`EXCLUDE_FROM_ALL`, links `xrCore`). Commands are in `src/xrUICore/tests/README.md`.
- Before correction, the standalone executable exited 1: `FAIL: crossed keys violate asymmetry`. After correction, it exited 0: `PASS: production shader key ordering; 24 cache insertion/lookup/erase permutations`.
- Standalone compilation uses production `xrstring.cpp`, `Threading/Lock.cpp`, `crc32.cpp`, and `xrMemory.cpp` with `USE_PURE_ALLOC`; integrated compilation uses the configured core allocator. Existing `_random.h` deprecated-volatile warnings were observed, with no new test warnings.
- The empty-group correction is two condition changes only. Code inspection confirms offline paths and nonempty commander copying are untouched, `update()` still exits through `bfActive()`, and `synchronize_location()` still returns `true`; this is not runtime proof of the empty-group path. Full build, direct guard coverage if available, bridge soak, and independent review remain integration responsibilities.

## 2. Private runtime and build — staging worker

Files: `tools/stage_coc_runtime.py`, `tools/tests/test_stage_coc_runtime.py`, staging documentation and runtime setup.

- [x] Stage the exact donor CoC base plus 1.4.22 patch into a private runtime, applying the target overlay last and preserving donor installs.
- [x] Test staging behavior and document inputs, destination, invocation, and manifest/provenance.
- [x] Initialize dependencies and build the target GL engine; this worker alone owns the full build.

## 3. Integrated verification and independent review — coordinator

- [x] Run focused tests against the integrated changes, including the production comparator executable.
- [x] Use the staged private runtime and agent bridge for new game, save, reload, menu, and quit; retain logs, screenshots, and exit evidence.
- [x] Run independent review of the combined diff and verification evidence before integration; no worker push/PR/merge.

## Review focus

Check online empty-group calls (no dereference, unchanged location), offline empty/nonempty behavior, nonempty commander propagation, crossed texture/shader cache keys and duplicates, and staging overlay precedence/source isolation. A normal game soak does not itself prove an empty online group was exercised; keep that coverage distinction explicit.

## Integrated verification record — 2026-11-07

- Stager: `python3 -m unittest tools.tests.test_stage_coc_runtime -v` passes 13
  fixtures (precedence, split archives, missing sources, restage/preservation,
  overlap/symlink/hard-link safety, interrupted managed updates, manifest validation).
- Complete tools suite: `python3 -m unittest discover -s tools/tests -v` passes
  41 tests, including four new frame-wait tests. Menu activation now precedes a
  bounded `wait_frames 2`; artifact existence alone is not visual proof.
- `git submodule update --init --recursive` succeeded. Installed SDKs were inspected;
  configuration used Release, unity, deployment target 15.0 and the available
  `/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk`; full `cmake --build build -j10`
  succeeded. Darwin `XRAY_EXCEPTIONS=0` was unchanged.
- `cmake --build build --target xrSimWorldStateTests xrUIShaderCacheTests -j10`
  succeeded; both `bin/arm64/Release/` executables exit 0. UI test confirms all 24
  cache permutations; `artifacts/ixray-foundation/integrated-tests.json` records results.
- Exact assets: base `/Users/rz/orca/ixray-1.6-stcop/build/runtime/coc-base/S.T.A.L.K.E.R. - Call of Chernobyl`
  and patch `/Users/rz/Downloads/S.T.A.L.K.E.R. - Call of Chernobyl 2`. Each staged
  runtime has 50 selected archives, 444 target GL shader files, and generated
  `fsgame.ltx`, with source paths and SHA-256 in `stage-manifest.json`.
- `build/runtime/coc` is the preserved user playtest runtime. Verification after
  handback uses `build/runtime/coc-verification` to protect even the user's autosave;
  no donor save/config or IXRay overlay was imported. Eight user files were backed up
  with hashes under `artifacts/ixray-foundation/user-playtest-backup`.
- Corrected menu smoke: `python3 tools/gl_macos_soak.py --game-dir "$PWD/build/runtime/coc-verification" --socket appdata/b.sock --scenario tools/gl_menu_smoke.txt --artifacts artifacts/ixray-foundation/menu-fixed --timeout 60`
  passes with exit 0; frame wait observed 4 to 8; CoC 1.4.22 menu JPEG was inspected.
- Full gate: `python3 tools/gl_macos_soak.py --game-dir "$PWD/build/runtime/coc-verification" --socket appdata/b.sock --scenario artifacts/ixray-foundation/fresh-cordon-final.txt --artifacts artifacts/ixray-foundation/fresh-cordon-final --timeout 240`
  passes in 109.904 seconds with normal bridge `cmd quit`, exit 0, no bridge/log
  failures, and RSS high water 2,167,840 KiB. Fresh stalker/rookie-village start
  reached `l01_escape`; movement changed X/Z from -212.24475/-144.41307 to
  -218.77637/-147.49820; own `foundation_gate_20261107` save reloaded at the latter
  position. World, reloaded scene, inventory, and exit-menu captures were inspected.
- Earlier interrupted runs were manually interacted with (user confirmed play);
  they are not engine-failure evidence. The initial black menu shots were the
  pre-menu intro framebuffer; the harness sequencing correction addresses that.
- Full gameplay gate used `rs_v_sync off`, without `-nosound`; the short corrected
  menu gate passed with vsync on. The earlier SDL3 swap-wait sample is retained;
  sustained vsync-on gameplay stability is unproven. Temporary verification settings
  are restored after the gate. Direct execution of an empty online group's guarded
  methods remains unproven and is not inferred from the normal-game soak.
- Opus independently approved the engine/stager changes, staging recovery delta, and
  final frame-wait harness delta with no material issues; it reproduced comparator
  red/green and passed all 41 tooling fixtures. The coordinator inspected the final
  diff, runtime report, preservation results, and inventory screenshot before local
  integration. No remote publication is part of this milestone.
- Final preservation: populated verification-runtime restaging succeeded; all eight
  snapshotted manifest/settings/save/config files retained identical hashes. All eight
  user-runtime files also remained unchanged; `preservation-results.json` records both.
