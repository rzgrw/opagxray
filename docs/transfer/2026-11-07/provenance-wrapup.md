# Provenance Windows transfer freeze

User-directed transfer ended the in-flight task task_8a8a737c1868 / ctx_3e41e04533eb. Coordinator requested wrap-up and then guarded worker-stop confirmed stopped with closed_agent_terminal. No game was owned by this worker. The final three-file bytes are checkpointed as WIP, not independently approved.

The last reviewed external-layout snapshot (tool65957771/tests6078e4e5/designf1d23d2e) had two reproduced P2 findings: case-insensitive macOS aliases bypass disjointness, and directory chmod or rename+symlink during socket observation can produce acceptance. Latest partial edits aim to repair these; they must be rechecked against the preserved reviewer external-probes.py, including all acceptance flags false, before claiming closure. See evidence/independent-reviews/orca-external-review-z3jq7lje/review.md.

The external contract is optional sealed launcher field external_communication_directory, with matching default-None inspect_runtime keyword. Runner must create a fresh canonical current-UID0700 directory outside runtime/application/source/settings aliases, pass one short absolute ordinary -agent_bridge endpoint, retain one bridge client and seal/attest before quit. Directory/socket identities are change detection, not PID inode ownership. Existing PID/executable/image/argv verification stays required. In-tree unexpected special files remain rejected.

Latest WIP requires independent delta review. No real live seal/attestation, overhead adapter integration, benchmark campaign or Windows process backend has been completed. Probe adapter remains on rzgrw/frame-overhead-reference with older pinned helper; do not update its pin without review/rebuild identity decisions. Historical failures remain unchanged.

Frozen SHA256:

- tools/frame_time_provenance.py edb3c3171e78e7d37e0f285f1e68e92e9273d781c76fe13afeca84c13eb74373
- tools/tests/test_frame_time_provenance.py 4ea555ed772b3733c82bc2cca73370f4634437d7727e8d7b1fd36ac2988bbb70
- docs/superpowers/specs/2026-11-07-frame-time-provenance-design.md cedc24fb4ff9068f52ad50a40873f19ef5b886aa85a5978675946f095ec64996

Coordinator freeze check: PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tools.tests.test_frame_time_provenance -q passed 55 tests in 1.252s. This is fixture evidence, not independent review or live acceptance.
