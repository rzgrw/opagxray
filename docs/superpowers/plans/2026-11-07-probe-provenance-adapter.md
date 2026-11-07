# Probe-specific provenance adapter

Task task_2ef5891b21eb / ctx_17f7e37eb0ba. Experimental worktree only;
no game launch, baseline/main edit, rebuild, commit or helper agent.

Approved probe C++/CMake/identity generator and all three binaries remain fixed.
Use byte-exact dependency snapshot from 90c6e57e697ce4fa16c306ab9a09f298c825f342:
provenance e8af674e3af5dcf6b068daeaec03259d8909b445e9013f888a00a854786e4366,
stager, reporter (not invoked on probe), and fsgame template.

- [x] Snapshot dependencies and approved role-specific identity/binary inventory.
- [x] Write small failing fixtures for source/binary/role/PID validation,
  strict probe measurement binding, missing/partial/expired seals, mutation,
  timeouts and exclusive failure artifacts.
- [x] Implement isolated dependency loading, reuse inspect_runtime and runtime
  compare/process scanning, replace only regular source/measurement validation.
- [x] Wire public seal(context)/attest(context,capture,evidence,seal) to the
  existing runner; persist failure results independently of caller exceptions.
- [ ] Verify focused fixtures, preserve approved file hashes, write exact new
  independent-review snapshot and report; live lifecycle/campaign remains next.

Design decisions: fixed reviewed inventory is the trust anchor, not caller hash
strings. Rehash generated inventory/source bytes/git state and all expected arm
binaries at both bookends; bind actually mapped engine images to that arm and
all additional non-OS images to the observed seal. Attest requires the original
PID alive. A fixture observation is never live attestation. Seal expiry and
settle timing are checked with monotonic time. Sidecar verification does not
grant campaign acceptance or convert unknown raw asset fields into checksums.


## Dependency gate discovered by the coordinator

The current e8af674e... pin was independently reviewed but rejected the expected
live AF_UNIX bridge socket as nonregular during the separate regular-provenance
smoke. No capture passed. Grok owns the exact launcher-bound socket correction;
this adapter does not bypass that check locally. The fixture
`test_expected_bound_bridge_socket_matches_actual_runtime_layout` reproduces the
failure using a real bound/listening socket and intentionally remains red until
the final independently reviewed helper commit/hash is supplied and pinned.

Before that new regression, 25 focused tests and all 92 tools tests passed.
All three actual generated source inventories and all 60 prepared binary hashes
were independently rechecked unchanged (no process launch). These are static
and fixture records, not proof that the current helper accepts a live layout.

- [ ] Receive the final reviewed helper commit and SHA-256, then snapshot its
  exact helper + stage/report/template dependencies under a new versioned path.
- [ ] Run the bound-socket regression and final focused/full tools verification.
- [ ] Produce the final independent-review snapshot after the dependency gate.
