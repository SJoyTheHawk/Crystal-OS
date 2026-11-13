# Phase 15/16 closure record

Updated 2026-11-14. The [implementation record](APP_PLATFORM_V2_PHASE_15_16_IMPLEMENTATION.md)
contains the contract mapping, design decisions and validation evidence.
This is a development milestone, not a production-readiness gate. Existing
results count; the user checks each changed core flow once. The agent handles
automated regressions. Hardening and deployment work do not keep these phases
open unless an observed issue prevents a core flow.

- [x] Scope follows the user's decision: WiFi browser panel plus existing USB
  CLI fallback. Browser USB is removed from Phase 15 scope.
- [x] Short authenticated log batches, browser/CLI cursor recovery, server-side
  sign-out, API action semantics, discovery and USB header bounds implemented.
- [x] Signed computer-side HTTPS catalog workflow implemented and tested;
  failed verification preserves a previously downloaded package.
- [x] Shared v1 UI facade implemented/reviewed: dynamic rich lists/charts,
  properties/events, focus and system keyboard, timers/animations and teardown.
- [x] Dashboard uses the Standard 200 ms startup warning class from the plan.
- [x] Existing A–R device evidence retained; host regressions and firmware build
  recorded in the implementation validation evidence.
- [x] Focused device recheck passed: **Focus input** keeps the keyboard open;
  a separate tap elsewhere dismisses it.

The core device check passed. The chart visibly changed once; list, dialog,
app switch, Weather, keyboard Enter and tap-outside dismissal passed. The host
regression exercises the actual Dashboard focus button, typing, Done, refocus
and outside taps. Phase 15/16 development acceptance is complete. No A–R rerun
or further device batch is required.

Public catalog URL and production signing-key ownership are deployment
configuration, not an unfinished browser store. Publishing requires an actual
hosting destination and release identity. Existing test key 240 remains for
local validation. The inherited HTTPS timeout/headroom findings remain
recorded for later investigation, without becoming additional phase-closure
requirements. A new failure that blocks normal use should be fixed when found.
