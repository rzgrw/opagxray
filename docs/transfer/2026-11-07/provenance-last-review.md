# External communication layout review — task_506a44d2416b

Verdict: CHANGES REQUESTED, two P2 correctness findings. The layout and trust separation are appropriate; these findings concern the promised filesystem disjointness and coherent directory observation, not socket-inode ownership or cryptographic authentication. No live acceptance was performed.

## Exact snapshot

Compared with approved tool 5bb2ea1f44b37bf031e40a9d09a05a51e3149e2df92483a90c79b3247bf8f65a. Before/after hashes match; source copies, snapshot.json and after-hashes.json retained here:
- tools/frame_time_provenance.py: 6595777112010acc74eef78747635456ee765b43fcb3d79f42e1d50e0f340595
- tools/tests/test_frame_time_provenance.py: 6078e4e585a8e25d46de43734e41e070a0a354743aa8db11859592b6a43f37df
- docs/superpowers/specs/2026-11-07-frame-time-provenance-design.md: f1d23d2e7390b8c29978a2e56e7ba35f50a8d46f4cba506401636c5fa63877c6

## P2: case-insensitive aliases bypass actual disjointness

Source: tool:681–684,743–759,817–819. `_contains_path` compares case-sensitive strings after realpath; on this actual macOS volume realpath preserves differently cased component spellings even when they address the identical inode. Thus its canonical check does not establish filesystem disjointness.

Reproducer (external-probes.py, repeated twice): create a short fixture application directory `/private/tmp/oxv-case-…`, with the fixture executable there; declare `/private/tmp/OXV-CASE-…`, bind its b.sock, and seal. `os.path.samefile(original, declared)` is true and `realpath(declared)==declared`, yet exit=0, asset_verified=true and association=external-observation. This directory also contains the fixture runtime/source roots, so both equality and ancestor exclusions are bypassed. Fixture live_runtime_attested remains false; this is a real filesystem alias, with simulated process/images.

Minimal correction: use filesystem identity when checking equality/ancestor overlap of existing directories, including both directions, or reliably obtain canonical component casing from the filesystem before containment checks. Do not simply lowercase all paths, which would misidentify distinct directories on case-sensitive volumes. Add a conditional case-insensitive-volume regression for application equality and manifest/runtime ancestor overlap; the same directory must be refused as external_directory before asset success. This is a normal macOS path-spelling issue, not an adversarial trust expansion.

## P2: mixed directory/socket snapshots allow successful attest after directory drift

Source: tool:799–845, especially directory lstat at810, validations817–823, callback825, socket lstat827, and returning the earlier directory fields839–844. Directory identity/mode/canonical status are never rechecked after looking up its child. The socket lstat follows intermediate directory symlinks.

Reproducer (external-probes.py, repeated twice): establish a valid external fixture seal, then use the existing after_lstat seam at the socket path (currently called before the socket lstat) to (a) chmod the directory from0700 to0755, or independently (b) rename the directory and replace its old pathname with a symlink to the moved directory. Attest in both cases returns exit=0, asset_verified=true and measurement_accepted=true, with no failures. In (b), the same socket inode is reached through the newly introduced directory symlink, while the returned directory identity is stale, so comparison against the seal misses the change. Actual nodes/permission changes are used; no lstat result is fabricated. Controls prove that either mode drift or directory replacement already present before inspection is rejected.

Minimal correction: acquire/validate a coherent directory-and-leaf snapshot, then recheck directory identity, type, mode, uid and canonical path after the child observation (and reject observed movement as stat_race); a pinned no-follow directory fd with relative no-follow child stat plus pathname identity verification is another bounded option. Retain deadline checks. Add both mutations above as failed seal/attest regressions with all acceptance flags false. This does not require proving the PID owns the socket inode.

## Verification

- 53 focused unittest tests PASS (focused.log).
- Unmodified previous socket18, earlier16, and followup24 assertions PASS, including the six original findings, ABI/104-byte bounds, C-locale token equivalence, failed-seal handling, vmmap spaced paths, file races, deadline tails, real-stager precedence and same-runtime fsltx.
- Six extra controls PASS: static0755/0770 rejection, executable mismatch, empty image set, directory replacement at the bookend, and explicit inspect keyword mismatch.
- Independent real bound external socket positive seal and attest PASS with fixture live flag false.
- New reproducer has three failing assertions corresponding to the two findings above; repeat exit1 and logs retained. Run with `PYTHONDONTWRITEBYTECODE=1 python3 <this-directory>/external-probes.py`; it creates fresh temp-only fixtures each time. Other scripts/logs are alongside it. Full claimed120 tools tests were not rerun.

## Retained guarantees and integration requirements

The declaration is covered by the raw launcher digest; optional inspect_runtime keyword defaults toNone and a nonmatching explicit value is refused. One ordinary explicit argv[1:] flag, absolute non-dot leaf, encoded103-byte bound, initial0700/current-euid checks, strict generator/fsltx rules, old in-tree scanning, PID/executable/images checks and existing deadlines are retained. external-observation correctly disclaims fd/inode ownership; mock process observation is not real attestation. Existing in-tree path-only live refusal remains unchanged. Spec says mode/uid replacement is launcher_identity, whereas an already-invalid mode currently yields external_directory before comparison; this is a minor documentation precision issue, not another acceptance defect.

The integration recipe is otherwise suitable: runner creates a fresh canonical0700 directory outside runtime/application/source/alias/settings trees, asserts its single short socket path absent, emits the exact launcher argv/field, retains one control client, runs seal and attest before quit, then cleans only its owned directory. Directory checks need the two corrections above before relying on that recipe. The runner/probe adapter is still separate: it may call inspect_runtime with the field omitted or equal to the sealed declaration, but must supply its own probe-schema handling/evaluation; regular frame attest still rejects the probe schema. No second client or inode-ownership claim is needed. Review the actual runner and then run a separately authorized live smoke before claiming runtime acceptance; the shader dotfile and other genuine asset failures remain failures. Historical failed seals were untouched. No source edits, game/GUI, full build, experimental-worktree changes, or publication occurred.
