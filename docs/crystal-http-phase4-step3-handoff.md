# Phase 4 Step 3 — KMB Stop Bounded Handoff

**Status:** Source/build gate passed; normal inbound/outbound handoff passed; lifetime validation pending.
**Plan:** [Phase 4 KMB route-stop plan](crystal-http-phase4-kmb-stops.md)

The KMB stop callback now copies the framework response body into the
PSRAM-owned `kmb_stops_handoff_t`, records status/error/attempts, releases the
framework response, and signals the bus worker. The worker owns the context
after the signal and cleans it after parsing. Timeout and cancellation retain
the context until a late callback can release and clean it safely.

The callback does not access LVGL or parse JSON. The CTB stop path remains on
the existing direct helper.

Validation completed:

```text
git diff --check = pass
idf.py build = pass
device normal handoff = passed for inbound and outbound
device timeout/cancellation lifetime matrix = pending
```

The normal run confirmed one response release for each direction. The next
device validation must still confirm one context cleanup for transport failure,
timeout, cancellation, and late-callback cases.
