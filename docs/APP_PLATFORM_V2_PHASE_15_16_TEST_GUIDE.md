# Phase 15/16 regression guide

Development acceptance is complete. Run only checks affected by a new change.
No full device checklist is pending.

## Start here

- [Implementation and frozen v1 UI contract](APP_PLATFORM_V2_PHASE_15_16_IMPLEMENTATION.md)
- [Closure and deferred work](APP_PLATFORM_V2_PHASE_15_16_CLOSURE_CHECKLIST.md)
- [Validation summary](test-evidence/phase15-16/2026-11-13-implementation-validation.txt)
- [Historical procedures and raw evidence](archive/phase15-16/README.md)

## Automated checks by change

Run from the repository root with resolved ESP-IDF dependencies. Use the IDF
Python environment for signature tests if system Python lacks cryptography.

| Changed area | Relevant checks |
| --- | --- |
| UI, declarative runtime, keyboard or lifecycle | `python3 tools/test_app_platform_v2.py`; `python3 tools/test_crystal_declarative.py` |
| Panel, API actions or logs | `node tools/test_control_panel.mjs`; `python3 tools/test_control_app_actions.py`; `python3 tools/test_control_logs.py` |
| Package signing or catalog download | `python3 tools/test_crystal_signatures.py` |
| Firmware | `idf.py build` in the configured ESP-IDF environment |

Tests must exit successfully. The platform suite intentionally prints failure
messages for rejected/corrupt fixtures; the following PASS results and successful
process exit distinguish them from an actual test failure.

## Device check when behavior changes

Use the matching board configuration and a Dashboard package signed with an
existing trusted key. See the [sample README](../examples/declarative_dashboard/README.md)
for packaging. Do not regenerate keys for a routine regression.

On S3, firmware changes use `idf.py -p <PORT> app-flash`; panel asset changes
also need `storage-flash`. JSON-only sample changes need repacking and
replacing the installed Dashboard. Follow the installer's restart indication
and pair the panel again after reboot.

Choose the affected flow:

1. **Panel/logs:** enable and pair the WiFi panel, start Live logs, launch an app,
   and press Refresh. Fresh lifecycle lines arrive and the panel stays responsive.
2. **Keyboard:** tap Focus input, type a note, then tap elsewhere. The opening
   tap keeps the keyboard open; the outside tap closes it. Done also dismisses
   it. Temporary text need not survive app teardown.
3. **Other widgets:** Add list item adds a row; Update chart shows a high-low
   zigzag; Show dialog opens and dismisses. The chart button applies fixed data,
   so subsequent presses need not visibly change it again.
4. **Lifecycle:** switch from Dashboard to Calculator and back. The keyboard
   closes on leaving and both apps remain responsive.

Check a changed flow once. Report a failure briefly with nearby diagnostics if
available. No long captures or repeat of passed batches is required. Combined-load
profiling, soak/recovery matrices, TLS timeout diagnosis and production catalog
publication remain later work, as recorded at closure.
