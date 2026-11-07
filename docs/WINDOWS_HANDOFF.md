# Windows transfer — 2026-11-07

Start here in the next agent session. The user is moving debugging and implementation to a Windows laptop. Mac worker contexts are frozen for transfer; do not resume old Orca dispatch IDs on the new machine.

## Goal and priorities

OpenXVibeRay should combine justified improvements from IXRay and OpenXRay with its own roadmap. Stability and frame-time consistency are first. Keep the exact CoC 1.4.12 base + 1.4.22 patch content; record each engine overlay separately. No local performance gain or comparative benchmark has been established yet. Independence means our own roadmap/releases; upstream tracking and selective intake remain allowed. Remote detachment, branding, license changes and wholesale renderer/physics imports have not happened.

Further AI authority expansion, native Metal/Vulkan work and broad DXMT/SDL/threading/audio migrations are held. Gate 1 Task 1 is integrated; the unmerged Gate 1 branch is recovery material, not ready to merge. Read PLAN_STATUS.md and the two OPENXRAY_*PERFORMANCE_AUDIT_2026-11-07.md reports.

## Checkpoints and worktrees

- `dev`: reviewed menu reload repair at `604fb0690`, on top of `90c6e57e6`. The repair passed 40 focused/sanitizer cases, lifetime construction checks, a consistent macOS Release GL build, independent review, and two private live early/settled saved-load lifecycles with normal exit 0. Windows is unverified.
- `transfer/windows-2026-11-07`: handoff branch containing that dev checkpoint, this document and the final provenance WIP. WIP preservation is not independent approval of its last delta.
- `rzgrw/frame-overhead-base`: `617957470b4c7b42458a40864a27147f6298578a`, WIP baseline experiment based on `e3f273f04`. Preserve independently; do not merge into dev.
- `rzgrw/frame-overhead-reference`: `d603c01d2e7b59cb661117f091f0acdadf6f6620`, WIP target/probe/adapter based on `456808010`. Preserve independently; do not merge the entire experiment into dev.

These WIP commits preserve previously uncommitted source files, tests, plans, the pinned helper copy under tools/_overhead_deps and all experiment controls. Existing Mac worktrees and ignored binaries remain on the Mac; a new Windows checkout must not copy their .git files or CMake caches. Bundled refs preserve all repository commits. Submodules are separate repositories: initialize the exact gitlinks, not their latest branches.

## Restore from the transfer directory (PowerShell)

Copy the entire `openxviberay-windows-2026-11-07` transfer directory to Windows. It contains the Git bundle, selected evidence, ref/submodule inventories and SHA256 manifest. It excludes game archives, credentials, Mac build trees and Mac binaries. Keep the directory outside the new source checkout.

```powershell
Get-FileHash .\openxviberay.bundle -Algorithm SHA256
# Compare with SHA256SUMS in the transfer directory.
git clone -b transfer/windows-2026-11-07 .\openxviberay.bundle C:\src\OpenXVibeRay
Set-Location C:\src\OpenXVibeRay
git remote set-url origin https://github.com/rzgrw/opagxray.git
git config core.autocrlf false
git submodule update --init --recursive
git submodule status --recursive
git status --short --branch
```

Use a short local NTFS path; avoid OneDrive/synchronization folders. Restoring the remote URL does not upload anything. No pushes or PRs were made for this transfer. To restore experiment branches from the same bundle, explicitly fetch `refs/heads/rzgrw/frame-overhead-base:refs/heads/rzgrw/frame-overhead-base` and the corresponding reference branch. Use a separate checkout for each, never overlay experiment files onto the main tree. A bundle is sufficient to restore the Git history; GitHub is needed for submodule downloads unless their source is copied separately.

## Windows build reality and first checks

Repository Windows CI uses Visual Studio/MSBuild with C++20, v143 (v142 fallback), Windows SDK 10, and NuGet:

```powershell
# In a Visual Studio Developer PowerShell with C++ desktop tools, SDK and NuGet installed:
nuget restore src\engine.sln
msbuild src\engine.sln /m:10 /p:Configuration=Release /p:Platform=x64
```

This is the existing CI recipe, not a verified build of today's branch. Inspect `.github/workflows/cibuild.yml` and `src/xray_project_configuration.props`. Start x64 Release, retain the first real compiler/linker log, and reconcile project source inventories before adding unrelated features. The recent xrSim/telemetry work was wired through CMake; checked-in `.vcxproj` files do not list FrameTimeCollector/FrameTimeSession and other recent sources. Windows CMake CI entries are commented out. Neither route is claimed working for the new work. Choose and fix one route deliberately; do not assume generating projects alone resolves dependency discovery.

Important platform gaps:

- `AgentBridge.cpp` implements the Unix socket bridge only outside XR_PLATFORM_WINDOWS. The Windows branch is a stub. `tools/agentctl.py`, GL soak scripts and the experimental runner depend on AF_UNIX/POSIX behavior. A Windows control transport is an explicit implementation task; do not claim bridge acceptance on Windows today.
- `FrameTimeSession::Export` returns `export_unavailable` on _WIN32. A Windows implementation needs equivalent exclusive, stopped-only, no-overwrite export behavior.
- `frame_time_provenance.py` uses macOS process/image inspection (libproc/vmmap); its external AF_UNIX metadata is not Windows process attestation. Keep unsupported platforms fail-closed or add a separately tested Windows adapter.
- The GL/CPU collector and portable tests are useful starting points. Some fixture tests exercise POSIX modes, sockets and Darwin ABI: full Python-suite counts from macOS are historical, not a promise every test runs on Windows.
- Darwin XRAY_EXCEPTIONS=0 remains mandatory for Mac. Do not globally force -fno-exceptions on unrelated engine files or change Windows exception policy to work around a port error.
- Rebuild all engine/game modules together: IGame_Level and MainMenu layouts changed. Never mix old DLLs with this source set.

## Current repairs and remaining findings

The original crash was a real SIGSEGV in Release: a non-null factory object failed the CUIDialogWnd cast, then ReloadUI wrote through null at 0x78. The guard now disposes rejected candidates correctly, restores only activation effects it acquired, guards failed reload notifications and uses level lifetime identity to prevent restoration to a same-address replacement. Registry Contains ignores invalidated entries. The live early test logged a handled factory/dialog rejection and continued to ready Cordon; the settled test did not log it. Both completed movement, private save/reload, menu/inventory recovery and acknowledged quit with exit 0. The factory's actual wrong dynamic type and lifecycle cause remain unknown. No audible playback claim was made.

A separate low follow-up remains: gamesave Screenshot can add duplicate multiplayer level seqFrame membership, then reload failure can skip its deferred removal. Preserve the review before fixing this; it was not deliberately exercised by the two single-player runtime tests.

The earlier supposed quit hang was unproven: cleanup opened a second client while retaining the first against a single-client server. All future harnesses must retain one connection, log send/ACK separately, quit on it, and take owned-PID thread evidence before timeout cleanup. A connect/send alone is not proof of command dispatch.

### Provenance work in progress

The regular telemetry parser and collector were independently approved. Raw unknown content/overlay metadata stay unknown; the separate sidecar can only close those specific provenance reasons, not invalid/truncated/empty measurements. Do not rewrite historical CSVs or failed seals.

The latest provenance source is checkpointed as WIP. Read evidence/main/frame-time/windows-provenance-wrapup.md for its final tests/hashes/review status. Earlier reviews found ignored redirects, incomplete vmmap, PID/file/deadline races, false AF_UNIX inode ownership, case-insensitive filesystem aliases and mixed directory/socket observations. The external layout declares a fresh runner-owned canonical 0700 communication directory outside all content/settings/application roots. Endpoint identities detect changes; sun_path does not prove inode ownership. PID/executable/images/argv validation and strict asset scans remain required. The latest two-finding fix still needs independent delta review if the wrap-up says so.

The retained gamedata/shaders/gl/.s dotfile is a genuine strict-scanner failure in a historical runtime; do not blanket-ignore dotfiles or retrospectively attest old captures. Stage a new clean runtime from known sources after deciding how to handle any unexpected file.

### Overhead experiment WIP

Preparation for three matched probe arms (baseline/off/on) built and passed focused tests on macOS. A separately reviewed preparation snapshot is not a completed benchmark. The probe schema is `xray-overhead-probe-v1`, parsed by tools/frame_overhead.py, never by the regular telemetry parser. The adapter branch pins an older provenance helper that rejects in-tree sockets; it has not adopted the final external-directory contract. Its accepted binary/source inventories are Mac-only. Any changed source/core/callsite invalidates those old build identities and requires rebuilding/redeclaring runs.

No five-runs-per-arm campaign occurred, no authenticated live measurement exists, and no overhead/speedup number is established. The common probe measures conditional overhead; it is not zero instrumentation. Windows results cannot be substituted for a Mac GL-versus-DXMT comparison: establish a new Windows baseline and label renderer, driver, hardware, power, cache, settings and assets.

## Assets, saves and evidence

Mac staging sources were:
- base: `/Users/rz/orca/ixray-1.6-stcop/build/runtime/coc-base/S.T.A.L.K.E.R. - Call of Chernobyl`
- patch: `/Users/rz/Downloads/S.T.A.L.K.E.R. - Call of Chernobyl 2`
- target GL shaders: this repository `res/gamedata/shaders/gl`.

Transfer the same base and patch archives separately to Windows (e.g. C:\Games\CoC-base and C:\Games\CoC-patch). The bundle and selected evidence do not include proprietary game content. Preserve base/patch precedence and compare the stored stage-manifest hashes. Do not use the old Downloads unpacked base-only tree, merge IXRay gamedata_coc wholesale or share IXRay saves. Each engine reloads its own save. Saved appdata/configs are separate from immutable content; preserve user playtest data on the Mac.

Python 3.11+ is needed for staging's hashlib.file_digest. The target stager copies archives and GL overlay into a private runtime; its Windows filesystem behavior still needs validation. Do not launch a Windows DirectX renderer with a GL-only shader overlay and call it equivalent. Decide GL versus DirectX explicitly and record the corresponding target shader layer. The runtime root/cwd and fsgame aliases must be checked on Windows before loading CoC.

The transfer evidence tree retains selected reports, manifests, patches, CSV diagnostics, crash IPS, logs, screenshots and independent review reproducers, omitting build directories, object files and binaries. Original ignored artifacts remain on the Mac. Asset manifests and binary hashes identify original Mac observations, not Windows validation. Reports can contain old absolute Mac paths; use their relative evidence suffixes. Start with:
- evidence/main/frame-time/windows-runtime-wrapup.md
- evidence/main/frame-time/runtime-stability-diagnosis.md
- evidence/main/frame-time/task-d09e1ea88ffc/report.json
- evidence/main/frame-time/task-1890b468aa80/reload-rereview.md
- evidence/main/frame-time/windows-provenance-wrapup.md
- evidence/overhead-reference/frame-overhead/ and independent-reviews/.

## Next session order

1. Verify transfer checksums, restore branch/submodules, read final wrap-ups and git status. Run no stale Mac dispatch.
2. Get a consistent Windows x64 build and focused portable regression tests; fix build inventory and platform gaps explicitly.
3. Stage the same CoC content privately; establish a controlled Windows play/save/reload/UI/quit baseline with inspected images and logs. Add a Windows bridge/export seam if automated testing is desired.
4. Complete independent review of provenance WIP and implement Windows process/transport support only as needed. Recover the overhead adapter selectively, never merge the whole experimental branch.
5. Measure first, then implement one bounded justified candidate: shared_str move-assignment correctness, missing GL sun/null-shader fixes or measured grass-shadow culling. Keep baseline/after evidence; upstream FPS claims are not local results.
6. Resume AI authority Tasks 2+ only after stability/comparison priorities; keep provider/model policy and pending contract questions from PLAN_STATUS.

Agent preference: implementations Codex, review Kimi when available; user explicitly authorized more Grok in this session. Kimi last fresh check hit its five-hour quota. Do not assume quota/reset state on Windows; do not start Claude without a fresh user request. Keep coordinator turns short and own worker lifecycle/cleanup. Preserve license notices and the donor restrictions documented in the audits.
