# Checkpoint evidence for Windows continuation

These compact reports preserve the final Mac context in GitHub. Read ../../WINDOWS_HANDOFF.md first. Large ignored artifacts, screenshots, original Mac binaries and full temporary review fixtures are preserved in the local transfer archive on the Mac; they are not required to restore source. Reports retain original Mac absolute paths as provenance.

- runtime-wrapup.md and runtime-report.json: both early and settled load/save/UI/quit runs passed; underlying factory cause remains unknown.
- menu-independent-review.md and menu-repair-report.json: reviewed repair and verification.
- provenance-wrapup.md, provenance-frozen.json and provenance-last-review.md: final frozen WIP and last outstanding review findings. The final 55 passing fixture tests do not close independent review.
- overhead-independent-review.md: preparation-only approval, no benchmark campaign or overhead number.
- NEXT_SESSION_PROMPT.txt: context to paste into the Windows agent.

Sources are on transfer/windows-2026-11-07; reviewed baseline on dev; separate experiment checkpoints on rzgrw/frame-overhead-base and rzgrw/frame-overhead-reference. No Windows build/runtime acceptance is claimed.
