# Frame-time asset provenance — design

**Date:** 2026-11-07
**Status:** Implemented by `tools/frame_time_provenance.py`. Fixture tests do not attest a live runtime. Historical captures are not rewritten.
**Checkpoint:** integration `a389540d83ea962f34c22dd7b0f422a514895b4d`
**Scope:** Correct the accepted sidecar before implementation. The gate is local asset attestation for one private runtime and one immutable raw capture.

## Purpose

`tools/stage_coc_runtime.py` writes `stage-manifest.json` (`openxviberay-coc-runtime-v1`) with base, patch, overlay, and a SHA-256 plus size for every managed file. The engine leaves `content_manifest` and `overlay` unknown. It does not read that manifest (`src/xrEngine/AgentBridge.cpp`).

The harness tool `tools/frame_time_provenance.py` uses stdlib `hashlib` only. It rehashes staged bytes and source bytes outside the measured interval and writes a sidecar. The raw CSV stays the bytes the engine exported. There is no signing service, no new package, and no engine hashing API.

## Trust model

This attests a local benchmark tree the operator can already read. It does not authenticate authors, licenses, or a hostile machine. The operator, the harness process, and the filesystems it reads are the trust boundary.

`result=verified` means only that the sealed asset and settings bytes matched at both bookends, that no unexpected scanned root was present, and that the sidecar names this CSV and the images actually loaded at seal. It does not mean the measurement passed.

Bookends do not watch the files continuously. A change made after the seal and reverted before the attest can pass. That limit stays accepted. Hashing does not run during `perf.start`…`perf.export`.

## Non-goals

- No engine API and no client manifest hash. The CSV keeps `content_manifest=unknown` and `overlay=unknown`.
- No rewrite of historical captures, including the 55-row export under `artifacts/frame-time/task-2287db229386/`.
- No change to `tools/frame_time_report.py` or to `--acceptance`.
- No classification fix and no overhead-reference design.
- No default toward an existing user runtime, and no read or write of one by the example or the tests.

## Three gates

**In-band acceptance** is `frame_time_report.py --acceptance`. `unknown` and `unavailable` on a required provenance field keep it false. A verified sidecar does not flip it.

**Asset attestation** is this tool's exit status: `0` verified, `1` mismatch, `2` unknown, `3` timeout. `verified` is not campaign acceptance.

**Measurement acceptance** is the sidecar boolean `measurement_accepted`. The sidecar also records `asset_verified` and `files_matched`. `files_matched` is the tree rehash only. `asset_verified` is that rehash plus the frame-time asset-field rule. `measurement_accepted` is the campaign gate. A verified asset result does not set `measurement_accepted`.

`measurement_accepted` is true only when `asset_verified` is true, the CSV is `xray-frame-time-v1`, `content_manifest` and `overlay` are still `unknown` or `unavailable`, `build_report(capture, scene="steady")` has no reasons left after removing only `unknown_provenance:content_manifest` and `unknown_provenance:overlay`, and no retained row has classification bit 6 (`window_visibility_unknown`). A steady row beside a visibility-unknown row is still rejected. A steady filter with no rows is `empty_selection`.

Any other reporter reason still rejects the measurement. That includes `truncated`, `dropped_loops`, `invalid_loops`, `empty_capture`, and `empty_selection`. `clock_is_steady=false` raises `CaptureError` before reasons exist; that schema failure rejects the measurement. `unknown_provenance` on `renderer`, `render_resolution`, `drawable_resolution`, `vsync`, `frame_limit`, `sdl_version`, `allocator_build`, or `source_build` rejects the measurement. The sidecar cannot satisfy those fields. `reporter_acceptance` is the reporter boolean and stays false while the two asset fields are unknown.

A pasted checksum in either asset field is an asset mismatch, even when the staged bytes match. The tool never writes the CSV. Parser `--acceptance` on the sealed happy path still exits 1.

The measurement plan's line about authenticated in-band content/overlay provenance should be read, once this tool exists, as this measurement boolean. Editing the plan is a later task.

## Stager format, from code

`filesystem_text()` in `tools/stage_coc_runtime.py` is the only fsgame generator. It reads `res/fsgame.ltx`, drops every line whose key starts with `$arch_dir` or equals `$game_arch_mp$`, replaces `$app_data_root$`, keeps every other template line in template order, and prepends six archive lines. The staged record source is the string `generated from res/fsgame.ltx`. Seal requires the staged `fsgame.ltx` bytes to equal `filesystem_text().encode()` from that module. A drifted generator fails closed. The bytes below are that function's current output, ending in one newline:

```text
$arch_dir$              = false| false| $fs_root$| database\
$arch_dir_config$ = false| false| $arch_dir$| config\
$arch_dir_maps$ = false| false| $arch_dir$| maps\
$arch_dir_resource$ = false| false| $arch_dir$| resource\
$arch_dir_sound$ = false| false| $arch_dir$| sound\
$arch_dir_patches$ = false| false| $arch_dir$| patch\
;abbreviation           = recurs|notif|  root|                  add|        ext|            description
$app_data_root$         = true| false| $fs_root$| appdata\
$game_data$             = false| true|  $fs_root$|            gamedata\
$game_ai$               = true|  false| $game_data$|          ai\
$game_spawn$            = true|  false| $game_data$|          spawns\
$game_levels$           = true|  false| $game_data$|          levels\
$game_meshes$           = true|  true|  $game_data$|          meshes\|       *.ogf;*.omf|   Game Object files
$game_anims$            = true|  true|  $game_data$|          anims\|        *.anm;*.anms|  Animation files
$game_dm$               = true|  true|  $game_data$|          meshes\|       *.dm|          Detail Model files
$game_shaders$          = true|  true|  $game_data$|          shaders\
$game_sounds$           = true|  true|  $game_data$|          sounds\
$game_textures$         = true|  true|  $game_data$|          textures\
$game_config$           = true|  false| $game_data$|          configs\
$game_weathers$         = true|  false| $game_config$|        environment\weathers
$game_weather_effects$  = true|  false| $game_config$|        environment\weather_effects
$textures$              = true|  true|  $game_data$|          textures\
$level$                 = false| false| $game_levels$
$game_scripts$          = true|  false| $game_data$|          scripts\|      *.script|      Game script files
$logs$                  = true|  false| $app_data_root$|      logs\
$screenshots$           = true|  false| $app_data_root$|      screenshots\
$game_saves$            = true|  false| $app_data_root$|      savedgames\
$downloads$             = false| false| $app_data_root$
```

`$arch_dir_patches$` adds the directory `patch\`, so the scanned path is `database/patch/`, not a runtime-root `patches/`.

Dropped template aliases are not effective after generation: runtime-root `mp/`, `levels/`, `resources/`, `localization/`, and `patches/`. Their presence is not an overlay and is not a failure. This design does not hash them and does not call them sealed content.

`archives()` copies only files under `database/` whose suffix matches `.db` plus optional digits. The shader overlay source is only `res/gamedata/shaders/gl`, copied to `gamedata/shaders/gl/<relative>`. No donor `gamedata` tree is staged. Manifest `files` values are `source`, `sha256` (64 lowercase hex), and nonnegative integer `size`.

## What the engine scans

`CLocatorAPI::_initialize` walks `fsgame.ltx` in order. The first field sets recurse. `Recurse` lists one directory level, and recurse descends. A name whose extension starts with `.db` or `.xdb` is `ProcessArchive`; other regular files are `Register`. `Register` replaces an existing virtual path (`LocatorAPI.cpp`, the existing-file branch). Later aliases therefore override earlier archive members. Archive aliases are first, so a later loose file under `gamedata/` overrides a database member with the same virtual name. POSIX `glob` does not list dotfiles; the gate still refuses dotfiles on asset roots.

Effective scanned paths, in generator order:

| Alias | Path under the runtime | Recurse |
| --- | --- | --- |
| `$arch_dir$` | `database/` | no |
| `$arch_dir_config$` | `database/config/` | no |
| `$arch_dir_maps$` | `database/maps/` | no |
| `$arch_dir_resource$` | `database/resource/` | no |
| `$arch_dir_sound$` | `database/sound/` | no |
| `$arch_dir_patches$` | `database/patch/` | no |
| `$app_data_root$` | `appdata/` | yes |
| `$game_data$` | `gamedata/` | no |
| `$game_ai$` | `gamedata/ai/` | yes |
| `$game_spawn$` | `gamedata/spawns/` | yes |
| `$game_levels$` | `gamedata/levels/` | yes |
| `$game_meshes$`, `$game_dm$` | `gamedata/meshes/` | yes |
| `$game_anims$` | `gamedata/anims/` | yes |
| `$game_shaders$` | `gamedata/shaders/` | yes |
| `$game_sounds$` | `gamedata/sounds/` | yes |
| `$game_textures$`, `$textures$` | `gamedata/textures/` | yes |
| `$game_config$` | `gamedata/configs/` | yes |
| `$game_weathers$` | `gamedata/configs/environment/weathers` | yes |
| `$game_weather_effects$` | `gamedata/configs/environment/weather_effects` | yes |
| `$level$` | `gamedata/levels/` | no |
| `$game_scripts$` | `gamedata/scripts/` | yes |
| `$logs$` | `appdata/logs/` | yes |
| `$screenshots$` | `appdata/screenshots/` | yes |
| `$game_saves$` | `appdata/savedgames/` | yes |
| `$downloads$` | `appdata/` | no |

Extension filters on `$game_meshes$`, `$game_anims$`, `$game_dm$`, and `$game_scripts$` are open masks, not scan filters. Any file those aliases reach can override an archive member.

## Sealed sets

The runtime is `--runtime`. Refuse a symlink runtime root. Do not follow links.

**Managed set.** Manifest membership must equal the managed files on disk: `fsgame.ltx`, the copied database archives, and `gamedata/shaders/gl/**`. Rehash each staged file and require the manifest SHA-256 and size. The stored digest is not a substitute for the read. Each non-fsgame source must be a regular file under manifest `base`, `patch`, or `overlay` by the stager's resolved-prefix rule, with the same SHA-256. A missing source is `unknown`. `files` must contain `fsgame.ltx` before that record is read. A manifest that omits it is mismatch `membership` and still writes the failure sidecar.

**Unexpected asset roots.** Refuse them. Do not claim exact content while one is present. Mismatch code `unexpected_path`:

- Any directory entry under `database/`, `database/config/`, `database/maps/`, `database/resource/`, `database/sound/`, or `database/patch/` that is not a manifest key, except the alias container directories `config`, `maps`, `resource`, `sound`, and `patch` directly under `database/`. Nested subdirectories, loose non-archive files, dotfiles, and `.xdb` or other engine archive suffixes the stager does not copy are unexpected. `gamedata/shaders/gl` is the shader container, not an extra file.
- Any directory entry under `gamedata/shaders/` that is not a manifest key and is not the `gl` container (or another directory that exists only as a parent of a manifest key).
- Any file directly in `gamedata/`, and any child of `gamedata/` other than `shaders`, `configs`, and the alias directories below.
- Any directory entry under `gamedata/ai`, `anims`, `levels`, `meshes`, `scripts`, `sounds`, `spawns`, or `textures`. An empty directory may exist. A file may not.
- Any file on a scanned path whose extension starts with `.db` or `.xdb` and is not a manifest key, including under `appdata/` and `gamedata/configs/`. Those are alternate archives.

**Settings set.** Hash these bytes at both bookends. They are file evidence, not the effective console state:

- The console script `execUserScript` loads. `x_ray.cpp` sets that name to `user.ltx`, or to the `-ltx` token in the launcher argv. `CCC_LoadCFG::Execute` strips one extension, appends `.ltx`, and opens the first hit under `appdata/`, then the runtime root, then the literal path.
- Every regular file under `gamedata/configs/`, which covers weather directories.
- The `cfg_load` closure of the console script. A `cfg_load` line is resolved the same way, recursively.
- The CInifile `#include` closure of every sealed `gamedata/configs` file. `CInifile::Load` recognizes a trimmed line that starts with `#` and contains `#include`, takes the second double-quoted item, and joins it to the including file's directory. The literal substring `*.ltx` means every loose `*.ltx` in that directory. Any other glob is `unknown`. A loose target is sealed and walked. A name with no loose file is archive-backed and has no separate hash. An include that leaves the runtime is `unexpected_path`.

`user.ltx` is a console script, not a CInifile, so a `#include` line inside it is not an include. `destroyConsole` later runs `cfg_save`. Attest before that rewrite.

**Live settings.** Effective values are the in-band fields `VerbPerf` writes at `perf.start`: `renderer` (`OpenGL` when an SDL GL context exists), `render_resolution` (`Device.dwWidth` x `dwHeight`), `drawable_resolution`, `vsync` from `SDL_GL_GetSwapInterval`, `frame_limit` from the live limit integer, `sdl_version`, `allocator_build`, and `source_build`. Unchanged `user.ltx` does not prove those values. Limits, recorded on the sidecar: swap interval 0 is exported as `off` for both disabled and indeterminate; a negative frame limit is omitted and stays unknown; resolution, vsync, and frame limit are marked unknown if they change while the session is running. Cvars that are not metadata fields are outside this attestation.

**Effective filesystem.** `x_ray.cpp` takes `-fsltx ` (the space is part of the match) and uses that file instead of the default `fsgame.ltx`. `LocatorAPI.cpp` takes `-overlaypath ` and retargets `$app_data_root$` and `$logs$`. The launcher record is an argv list. A separate `-fsltx` token plus the next element, or one element that contains `-fsltx `, is the effective file. It is accepted only when its realpath, relative to the runtime when the token is relative, is the runtime's own `fsgame.ltx`, and those bytes still equal `filesystem_text()`. Any other `-fsltx` is mismatch `filesystem_redirect`. `-fsltx=` has no space and is not a redirect. Any argv element that contains `-overlaypath` is `filesystem_redirect`, including one that names the runtime. The tool does not scan the foreign tree. `-ltx` stays the console-script selector.

**Launched images.** There is no `--binary` list. `--launcher-record` is required. The harness that calls exec writes `openxviberay-launcher-record-v1` with `pid`, the exec path, `argv`, and `cwd`. At seal the pid must be alive, `proc_pidpath` from system `libproc` (ctypes, no new package) must equal the record's executable, and `/usr/bin/vmmap -w <pid>` must exit 0. Mapped images are `vmmap -w` region rows. The address range is its own column, the hex-hex field immediately before the bracketed size, followed by protection, `SM=`, an optional `PURGE=`, and the region detail. The detail is the whole remainder of the row, so a mapped path may contain spaces. A hyphen inside a header path is not an address range: `Path: /tmp/a-b/xr_3da` is not a mapping. A bare path, a non-zero vmmap, or a file-bearing row that does not match this column grammar is not a partial image list. Failed, empty, header-only, or unparseable output is `unknown` `process_unverified` and does not set `live_runtime_attested`. Every parsed non-OS region path is kept. The vmmap subprocess timeout is the remaining deadline, or 30 seconds when no deadline is active. Do not fall back to five name-matched files.

Hash every mapped image that is not under `/System/` or `/usr/lib/`. Record OS images by path only. For each hashed image, record Mach-O `LC_ID_DYLIB` current and compatibility versions when the stdlib parse succeeds; a failed parse of a hashed image is `unknown`. Record `LC_LOAD_DYLIB`, `LC_LOAD_WEAK_DYLIB`, and `LC_REEXPORT_DYLIB` install names from the main executable as configured linkage, not as a second content proof. The loaded path is the image identity. An extra non-OS image is hashed and listed; it cannot pass silently. The claim stops at that set.

Seal stores the normalized runtime, launcher pid, the realpath of the executable, argv, the realpath of cwd, and the SHA-256 of the raw launcher-record bytes. Attest compares them in that order and raises mismatch `launcher_identity` on the first change. This is the recorded launcher identity, not a kernel process-start token. PID reuse after the process exits, while that launcher record is left unchanged, is an observation limit.

At attest, rehash the sealed image paths. If the process is still alive, `vmmap` must equal the sealed path set. A dead process cannot show a new load; only the sealed paths are rehashed.

**Source string.** Recompute the CMake string in `src/xrEngine/CMakeLists.txt`. It is `XRAY_GIT_SHA` immediately followed by `name:sha256;` in this order: `FrameTimeCollector.h`, `FrameTimeCollector.cpp`, `FrameTimeSession.h`, `FrameTimeSession.cpp`, `FrameTimeClassification.h`, `device.h`, `device.cpp`, `x_ray.cpp`, `AgentBridge.cpp`, `CMakeLists.txt`, then `Layers/xrRenderGL/glHW.cpp`, `Layers/xrRender/r__sync_point.cpp`, `Layers/xrRender/QueryHelper.h`. SHA-256 of that text is `telemetry_sha256`. With `--config Release`, the CSV `source_build` must equal `git:<HEAD>,telemetry_sha256:<hash>,config:Release`. HEAD is `git rev-parse --verify HEAD`, the same read as `query_git_info`. A dirty file outside this list is invisible. `AgentBridge.h` is not in the list. A matching string is not the executable identity. Image hashes are that identity. A matching string with a different loaded executable is a mismatch.

**Manifest bytes.** Seal records SHA-256 of the raw `stage-manifest.json` bytes. Attest requires those exact bytes. A re-serialization is a mismatch even when the JSON value is equal. Managed keys must be unchanged.

**Unbound writes.** Record pre/post aggregates for `appdata/cdb_cache`, `savedgames`, `logs`, `screenshots`, and `perf`. A change there does not fail asset attestation except for an unexpected archive suffix. The sidecar lists the changed groups. This design does not define a cache-stability rule for the campaign.

## Path and race rules

Relative paths are Unicode POSIX, with no empty, `.`, or `..` segment, no backslash, no repeated slash, no trailing space or dot, and no code point below U+0020 or equal to U+007F. After resolving, the canonical path must equal the lexical path under the runtime. Otherwise mismatch `ambiguous_path`.

Walk every directory entry in the settings closure, not only regular files. A symlink file or symlink directory is mismatch `symlink`. Any other non-regular entry is mismatch `nonregular`. The failure names the path. Do not treat either as success or as `unknown`.

Open with `O_NOFOLLOW`, then `fstat` the fd. The accepted snapshot is `st_dev`, `st_ino`, size, `mtime_ns`, and `ctime_ns`. `lstat` and that `fstat` must match before the read. After the streamed read, `fstat` the same fd and `lstat` the path again. If either disagrees with the accepted snapshot, or the hashed length disagrees with that size, the result is `unknown` with code `stat_race`. A same-size in-place mutation is that race, not a verified hash of the bytes first read. Do not report mismatch or verified for that read. An entry that appears or disappears during the walk is the same `unknown`. When the stat snapshot moved and the deadline is also exceeded, `stat_race` is the result.

## Aggregates

Managed, settings, and loaded-image aggregates use the same blob: UTF-8 lines sorted by relative or image path, each `path\0sha256\0size\n`, then SHA-256. OS images are a separate sorted path list and are not in the image aggregate. An empty settings closure hashes the empty blob. Absolute source paths are recorded and are not part of the aggregate.

## Timing

Reach the ready scene before sealing. Seal reads the large archives and warms the OS file cache. The harness then waits the required `--settle-seconds` and records that declared interval on the seal. `perf.start` is after the wait. The capture is a post-seal, post-settle measurement. It is not a cold file-cache measurement. The tool records the declaration; it does not time the operator's wait.

The budget defaults to 180 seconds per command, on `time.monotonic()`. The deadline is checked before every file and before every 1 MiB read of that file. A single large file cannot run past the budget. `--timeout-seconds 0` is `timeout` before the first chunk. The same deadline is checked after non-streamed work, including source identity, and again before a successful snapshot is returned. Attest checks it again after comparison, capture binding, report evaluation, and the final seal-byte read, immediately before returning a verified result. A clock that expires inside report evaluation is exit 3, with `asset_verified` and `measurement_accepted` false. `git rev-parse` and `vmmap` receive the remaining budget as their subprocess timeout. `source_build` keeps a one-argument `(config)` signature; the active deadline is ambient so a replacement with that signature still runs. A command that finishes after the deadline is exit 3. Timeout writes a sidecar whose `result` is `timeout` and whose `asset_verified` is false, never `verified`.

## Commands

The example runtime is `/tmp/frame-time-provenance-example`, a disposable fixture. Tests build their own temporary directory. This path is not the stager's default output. Leave existing user runtimes untouched. `--out` must not exist. The tool never opens the raw CSV or the evidence copy for writing. The harness copies the export to `--evidence` first. Attest requires equal bytes and equal length.

```text
python3 tools/frame_time_provenance.py seal \
  --runtime /tmp/frame-time-provenance-example \
  --launcher-record artifacts/<run>/launcher-record.json \
  --config Release \
  --settle-seconds 30 \
  --timeout-seconds 180 \
  --out artifacts/<run>/provenance-seal.json

python3 tools/frame_time_provenance.py attest \
  --runtime /tmp/frame-time-provenance-example \
  --launcher-record artifacts/<run>/launcher-record.json \
  --seal artifacts/<run>/provenance-seal.json \
  --capture /tmp/frame-time-provenance-example/appdata/perf/capture-<pid>-<n>.csv \
  --evidence artifacts/<run>/accepted-runtime-capture.csv \
  --timeout-seconds 180 \
  --out artifacts/<run>/frame-time-provenance.json
```

A non-zero asset result still writes the sidecar when `--out` was free. `result=verified` is written only with exit 0.

## Sidecar

`format` is `openxviberay-frame-time-provenance-v1`. `trust` is `local-asset-attestation`.

The seal records runtime, manifest format, raw `manifest_sha256`, base, patch, overlay, managed count and aggregate, settings aggregate and the console-script path, `settle_seconds`, launcher pid, executable, argv, cwd, and `launcher_sha256` of the raw launcher-record bytes, the explicit `fsltx` path when one was accepted, hashed images with optional dylib versions, OS image paths, configured load-command names, recomputed `source_build`, the settings limit string, and unbound aggregates.

The attest records the seal path and seal SHA-256, the second aggregates, `managed_unchanged`, `settings_unchanged`, `manifest_unchanged`, `fsgame_matches_generator`, per-group unbound changes, `capture_sha256`, `evidence_sha256`, the in-band provenance strings, reporter `reasons`, `result`, `measurement_accepted`, and `failures` as `{code, path, detail}`.

`verified` requires managed bytes equal to the manifest, the sources, and the seal; exact fsgame generator bytes; required `fsgame.ltx` manifest membership; exact manifest bytes; no unexpected scanned entry; no filesystem redirect; settings closure unchanged; a successful vmmap whose region records were kept; loaded-image set and hashes unchanged; launcher pid, executable, argv, cwd, runtime, and raw launcher-record digest unchanged; launcher pid bound to `proc_pidpath`; CSV asset fields still unknown or unavailable; and `source_build` equal to the recomputed string. Missing, unreadable, or raced input is `unknown`. A compared hash or a forbidden path is `mismatch`. A blown budget is `timeout`.

## Workflow

1. Stage with `tools/stage_coc_runtime.py` into an explicit directory the operator chooses. Stop the game before a restage. Do not restage a runtime that is in use.
2. The harness execs the real binary and writes the launcher record. Do not issue `perf.start`.
3. Reach the ready scene and leave it idle.
4. Run `seal`. On any non-zero exit, do not capture.
5. Wait the declared settle interval. No provenance process runs after that wait starts.
6. `perf.start`, the measured scene, `perf.stop`, `perf.export`.
7. Copy the exported CSV to the evidence path.
8. Run `attest` before a settings rewrite, a restage, or quit (`cfg_save`).
9. Accept the sample only when `measurement_accepted` is true. Keep the raw CSV, the evidence copy, the seal, and the sidecar.

A later attest after a restage or an edit fails. A capture with no seal is `unknown`. There is no backfill from the manifest alone.

## Tests

`python3 -m unittest tools.tests.test_frame_time_provenance -v`

Temp fixtures in the style of `tools/tests/test_stage_coc_runtime.py`. No proprietary archives, no game launch, and no existing user runtime as input.

- Happy path on a temp runtime: seal, CSV with the two asset fields unknown, attest exit 0, `measurement_accepted` true, evidence hash equal, reporter `--acceptance` still exits 1, CSV bytes unchanged.
- Repeat the happy path and require the same managed aggregate.
- Flip one staged byte after seal: attest exit 1 and the failure names that path.
- Loose file under `gamedata/scripts`, `meshes`, `textures`, `sounds`, `anims`, `ai`, `spawns`, or `levels`: exit 1 `unexpected_path`. An empty `gamedata/scripts/` directory still seals.
- Extra file under `gamedata/shaders/` outside `shaders/gl`, a file directly in `gamedata/`, or a child directory other than the generated aliases: exit 1.
- Extra `.db` or `.xdb` under `database/patch/` or `appdata/`: exit 1. A runtime-root `levels/`, `resources/`, `localization/`, `mp/`, or `patches/` directory does not fail and is not listed as sealed content.
- `cfg_load` target and a `#include` target are in the settings aggregate. `#include "*.ltx"` includes every loose `*.ltx` in that directory. An include outside the runtime is exit 1. A `#include` line in `user.ltx` is not followed.
- Change `appdata/user.ltx` or a sealed config: exit 1. The same bytes with CSV `frame_limit=unknown`: asset exit 0 and `measurement_accepted` false.
- Symlink directory or symlink file under `gamedata/configs`: exit 1 `symlink`. A fifo there: exit 1 `nonregular`.
- A path containing a newline, a `..` segment, or a canonical path that differs from the lexical path: exit 1 `ambiguous_path`.
- File replaced between `lstat` and read, or rewritten in place at the same size during the read: exit 2 `stat_race`, not mismatch and not verified.
- `fsgame.ltx` bytes differ from `filesystem_text()`: exit 1. Manifest membership gains or loses a key, including a manifest that omits `fsgame.ltx` while the file remains on disk: exit 1 `membership` and a sidecar. Manifest whitespace changes after seal while the JSON value stays equal: attest exit 1.
- An explicit `-fsltx` whose realpath is the runtime `fsgame.ltx` seals. A foreign `-fsltx`, a one-element `-fsltx /other`, or any `-overlaypath`: exit 1 `filesystem_redirect`, and attest of that sidecar is `seal_not_verified`. `-fsltx=` is not a redirect.
- Launcher pid's `proc_pidpath` differs from the record, or `vmmap` is unavailable, non-zero, header-only, or only a `Path:` line whose path contains a hyphen such as `/tmp/a-b/xr_3da`: not verified, and `live_runtime_attested` stays false. A region row keeps the executable and every other mapped path, including a dylib path that contains spaces. A file-bearing row that is not the column grammar refuses the observation. An extra non-OS mapped image is hashed into the image aggregate; attest exits 1 if that path's bytes change, including the spaced path, or if a still-live `vmmap` set differs from the seal. Five name-matched paths and no launcher record: exit 2.
- Launcher pid, argv, or raw launcher-record bytes change between seal and attest: exit 1 `launcher_identity`.
- A clock that expires during source identity, a git timeout, a vmmap timeout, or report evaluation on attest: exit 3, sidecar `result` is `timeout`, and the acceptance flags stay false.
- CSV `source_build` matches the recomputed string while the loaded executable hash differs: exit 1.
- `--timeout-seconds 0`, and a budget that expires on the second chunk of one large file: exit 3, sidecar `result` is `timeout`.
- Truncated CSV, `invalid_loops`, empty capture, empty steady selection, and `clock_is_steady=false`, each with matching assets: asset attestation does not make `measurement_accepted` true.
- Measurement is true only when the sole provenance reasons are the two asset fields and the sidecar is verified.
- Pasted manifest checksum in `content_manifest`: exit 1 whether or not staged bytes match.
- Edit the evidence CSV after a verified attest and attest again to a new `--out`: exit 1.
- Existing `--out`: exit 2 and the old bytes stay.

## Limits

- The operator can point the tool at a tree they already made self-consistent. The check catches drift, stale copies, extra scanned files, and post-hoc CSV edits.
- Quit-time `cfg_save` rewrites the console script. Attest first.
- Cache, save, log, screenshot, and perf writes stay visible and can still leave asset `result=verified`.
- Seal warms the OS file cache. The sample after the declared settle interval is not a cold-cache result.
- `source_build` covers git HEAD plus the listed files only. It is not the executable, the linked externals, or a dirty tree outside that list.
- `/System` and `/usr/lib` images are named, not content-attested. Unmapped files are outside the image claim. A mapped path keeps its internal spaces. A file-bearing vmmap row that does not match the region columns refuses the observation.
- Launcher identity is the sealed record, not a kernel start token. PID reuse after exit, with that record unchanged, is not detected.
- Swap interval 0 is not a precise vsync observation. Console cvars absent from the metadata list are not sealed.
- Dropped template roots are not scanned with this generator. A hand-edited `fsgame.ltx` fails the generator byte check instead of being reinterpreted.
- Windows is unspecified. The image observation is the current macOS GL campaign.
- Provenance syntax in the reporter is still not attestation. `asset_verified` and `measurement_accepted` remain different answers.
- Revision `e3f273f04bf85b4b265004cbbc8a35f19796cd31` is not a telemetry fingerprint. This tool does not claim that commit contains `FrameTimeSession` or a collector export.

## Reusable asset helpers

These functions do not import or call `frame_time_report`: `hash_regular`, `aggregate`, `parse_macho`, `is_os_image`, `images_from_vmmap_text`, and `inspect_runtime`. `inspect_runtime` rehashes the staged tree, settings closure, and observed images. A later caller can reuse that snapshot without accepting a measurement.

`source_build` is only the regular telemetry CMake string (`git` HEAD plus the listed engine and render files). It is not a probe source snapshot and it does not read `source-identity.json`.

`evaluate_frame_time_measurement` is the only schema gate. It accepts `# xray-frame-time-v1` and calls the reporter. If the first line is `# xray-overhead-probe-v1`, it returns `unexpected_schema` with `reporter_invoked` false and does not call the reporter.

## Probe adapter, not implemented

Codex owns `tools/frame_overhead.py` in the overhead worktree. That parser is `xray-overhead-probe-v1`: one metadata block, columns `loop_seq,start_ns,wall_ns`, and hash fields `probe_core_sha256`, `callsite_patch_sha256`, `source_snapshot_sha256`, `binary_sha256`, and `user_ltx_sha256`, plus revision and collector envelope counts. Its base role pins revision `e3f273f04bf85b4b265004cbbc8a35f19796cd31`. This sidecar does not parse that file and does not treat that revision as a frame-time source.

The remaining adapter, which this change does not build, would call `inspect_runtime` (or its recheck), bind mapped images, and compare probe identity to a generated `source-identity.json` supplied by that workflow. The parent's stated completion rule is one inherited pipe byte after the final stamp, collector closure, and export, with the process still alive for an explicit attest before a normal bridge quit. The parent wires that contract after both reviews. Until then, `attest` on a probe CSV is `unknown` / `unexpected_schema`, and `live_runtime_attested` stays false for fixture observation.
