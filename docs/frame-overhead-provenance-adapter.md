# Experimental probe provenance adapter

Task `task_2ef5891b21eb`, dispatch `ctx_17f7e37eb0ba`. Only the experimental
reference worktree is changed. The previously reviewed C++ probe, callsites,
CMake, identity generator, probe parser and all three binary sets are preserved.
No main/baseline source edit, rebuild, game launch, live probe attestation or
benchmark campaign is part of this task.

## Current dependency blocker

The coordinator's separate live regular-provenance smoke found that the current
reviewed e8af674e... helper rejects the legitimate `appdata/overhead.sock` Unix
socket as nonregular. A real bound/listening AF_UNIX fixture reproduces this
failure. **This snapshot is not ready for live use.** The helper author owns the
narrow launcher-bound exception; no adapter bypass is present. Final pinning,
verification and independent review wait for the corrected reviewed commit/hash.
Earlier green tests did not include this actual socket node and are not final
acceptance evidence. No probe capture or live attestation is claimed.

## Pinned dependencies and trust

`tools/_overhead_deps/v90c6e57e6/` contains exact Git-object bytes from commit
`90c6e57e697ce4fa16c306ab9a09f298c825f342`:

| File | SHA-256 |
|---|---|
| `tools/frame_time_provenance.py` | `e8af674e3af5dcf6b068daeaec03259d8909b445e9013f888a00a854786e4366` |
| `tools/stage_coc_runtime.py` | `cdc36dda629fa621c34bdb0d4a2b105509c63ccceb1a194b282d37829399118c` |
| `tools/frame_time_report.py` | `f0f6a8ee084154b10425767faa0d0e7bd0d8bd9a19fcbfe0d2ac17752ab23f50` |
| `res/fsgame.ltx` | `22de48b67b46718026af451e731c7d32f8b0deeb47738c9eb952d47304a4341e` |

The adapter verifies these hashes before executing unchanged module bytes in an
operation-local namespace. The upstream module's absolute stager import is bound
to the snapshot stager, and its `sys.path` change affects a private copy. The
stager's `__file__` binds its generator to the snapshot template. Ambient `tools`
imports cannot supply dependencies. The regular reporter is integrity-checked
but its import is explicitly forbidden in this adapter.

`tools/frame_overhead.py` remains the strict probe parser and is separately pinned
to approved SHA-256
`df5d9713e18dee39d784a801a3ddd453d4ae00fd01fa849f2a1d21a7e5efc926`.
It is loaded from verified bytes by explicit sibling path; no regular telemetry
parser runs on a probe capture.

`tools/overhead_approved_builds_v1.json`, SHA-256
`279ce48ecdbcac918db0a117921db6edace4a4592a96142e580d5933f9de8b61`,
is an explicit extraction of the approved task_5feca4024e59 build record. It pins
source roots, exact generated source-identity JSON bytes, role/revision/core/
callsite/source hashes and all 20 prepared images per arm. Public operations
always load this hash-pinned inventory; caller configuration cannot substitute
an inventory or fixture process. Paths are intentionally tied to the preserved
worktrees/artifacts. Relocation or rebuilt images need an explicitly reviewed
new inventory; there is no moving fallback to HEAD, main, or same-named files.

This is local bookend attestation under a trusted operator/filesystem model, not
a signing service or defense against a malicious operator who changes the code.
The new harness has its own independent-review hashes, separate from unchanged
compiled probe source identities.

## Public runner interface

```sh
python3 tools/frame_overhead_runner.py \
  --run /absolute/private-run-config.json \
  --adapter tools/frame_overhead_provenance.py
```

This is a future live command, not executed in this task. Without `--run`, the
runner still only prints the schedule. The adapter never launches a process.
It implements `schema`, `seal(context)` and
`attest(context, capture_path, evidence_path, sealed)` from the runner contract.

The existing run config supplies role, source_identity, binary, runtime, out,
scene/save/level/camera, machine, counts, run index and settle interval. The runner
supplies its exact launcher record, live PID and selected private save hash.
Optional settings are `provenance_timeout_seconds` (default 180 per operation)
and `provenance_max_age_seconds` (default 600; must exceed the settle interval).
No implicit path defaults select a user runtime. Seal and attest are called only
outside the probe window, before normal quit/settings rewrite.

The adapter persists `adapter-seal.json` or `adapter-attest.json` independently of
the runner's `seal.json`/`provenance.json`. Every write is exclusive. Existing,
partial or unavailable outputs cause failure and are never overwritten. When the
output is available, rejected inputs, source/image failures, expired seals and
timeouts leave a structured failure artifact. Failed bootstrap pin checks use a
minimal stdlib failure writer instead of executing an unverified dependency.

A result is compatible only if it has the expected format/schema/command, a
verified result, all required identity/asset booleans, no failures, and valid
monotonic expiry. The runner additionally requires actual live observation;
fixture-only matches cannot arm capture or pass attestation. Attest also requires
schema validation, the accepted schema/asset measurement gate, and an exact raw
capture hash. Campaign acceptance is always false here. Seal results expire at
the declared seal lifetime; attest results expire after 60 seconds.

## Verification performed at both bookends

1. Verify the generated role-specific source-identity file against its approved
   raw-byte hash and exact expected values. Require the pinned source HEAD.
2. Rehash every source file listed by that generated inventory with the inherited
   race-aware regular-file reader; recompute the canonical source aggregate,
   identical core concatenation hash and the separately hashed Git callsite diff.
   Verify pinned recursive submodule state and no tracked dependency edits.
   Base does not need or access `FrameTimeSession`; its inventory has no such file.
3. Rehash every expected arm binary and check source/core/callsite/revision
   identity strings embedded in its approved xrEngine image. Match the requested
   executable to the arm's exact executable path.
4. Reuse upstream `inspect_runtime` without changing its asset, filesystem,
   settings, source-asset, launcher, process or mapped-image logic. Only its
   regular `source_build(config)` callback is replaced with the verified probe
   source binding. The callback cannot require nonexistent baseline telemetry.
5. Require the exact launcher PID and executable, alive at both seal and attest.
   Bind every mapped engine-image basename to the expected arm path and hash;
   require at least the actual executable and xrEngine mapped. Unexpected files
   in the build directory fail. Other mapped non-OS images are fully hashed by
   the upstream inspector, recorded, and must retain their exact set and bytes
   across bookends. OS path sets are also compared, without a content-hash claim.
6. Hash the actual console script selected by the upstream argv/filesystem
   resolution, and the explicitly selected private save. Save changes are a
   failure even though the upstream tool treats the broader saves tree as an
   unbound-write group.

Attest requires an unchanged, complete, successful `adapter-seal.json` matching
the supplied seal and context. It verifies settle/expiry timing, checks the
sealed snapshot digest, repeats the source/binary/runtime checks, and uses the
upstream comparison for unchanged assets/settings/launcher/images. It compares
OS image sets, configured load names and probe-specific bindings as well.

The raw/evidence paths must be this PID's exported probe CSV and the runner's
`probe.csv`. Both files must match by bytes/hash/length. The strict probe parser
validates the schema, counts, flags and collector envelope, then the adapter
compares revision/core/callsite/source/executable/settings identity, role, scene,
save, level, resolution, machine, counts and run index against the verified
context. It rebinds raw/evidence and seal bytes after parsing. Deadline checks
cover source/git/runtime work, the final capture/sidecar reads and serialized
result preparation; expiry cannot become a verified output.

## Acceptance limits and fixtures

`measurement_accepted` means valid probe schema plus verified local asset/source/
image bookends under a live observation. It is not a full-window functional or
performance verdict. Raw `content_manifest` and `overlay` stay `unknown`, and
`in_band_acceptance` stays false. No regular reporter verdict is manufactured.
Fixture operations may report matching files with `live_runtime_attested=false`
and `measurement_accepted=false`; the public runner rejects them.

The fixtures exercise all three roles (including base without FrameTimeSession),
correct source/binary binding, wrong PID/executable/arm/image sets, source and
identity mutation, malformed probe/forged metadata, runtime override roots,
settings/source-asset/save changes, raw/evidence mismatch, missing/partial/failed/
expired seals, early and late timeouts, bootstrap pin failures and no-overwrite
behavior. Existing runner pipe tests use a disposable fixture executable, never
the game. Their stub adapter tests transport sequencing, not live provenance.

The inherited trust limits remain: files changed and restored between reads are
not continuously observed; PID identity has no kernel process-start token;
OS images are path-only; current on-disk images are the local mapped-path claim.
No game/live probe was used to validate those observations in this task. Actual
probe lifecycle, main-menu/readiness behavior, output and pipe failure recovery,
full-window functional/traffic/log/exit gates, and the five-run-per-arm campaign
remain for the next exclusive runtime assignment. The common-probe and integer
501-limiter limitations remain unchanged; no absolute overhead result is claimed.
