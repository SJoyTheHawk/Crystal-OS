# Phase 4 Step 6 — KMB Stop Cancellation and Stale Requests

**Status:** Passed — source, build, and device cancellation validation complete.
**Plan:** [Phase 4 KMB route-stop plan](crystal-http-phase4-kmb-stops.md)

## Decision

KMB stop requests must be cancelled when the user leaves the stop page. The
request is user-visible work tied to that page, so allowing it to consume the
remaining retry budget after Back is a lifecycle defect. Network loss and app
pause/destroy must also cancel the active KMB stop owner.

## Problem found in device validation

The supplied run showed Wi-Fi loss during outbound stop request `framework_id=9`
at about `315829 ms`, but the framework continued through attempts `2/3` and
`3/3`. The final stats reported `cancelled=0`. The existing network handler
only cancelled the catalog owner, and the stop-page Back path only hid the page;
it did not cancel the active KMB stop request.

## Change implemented

- Added `bus_service_cancel_stops()` to cancel `KMB_STOPS_OWNER_ID`.
- The stop-page Back handler calls it before hiding the page and clears
  `stop_request_id_`, so a racing callback cannot update the hidden page.
- `bus_service_network_disconnected()` now cancels the KMB stop owner as well
  as the catalog owner.
- `bus_service_cancel_all()` now cancels both framework owners for app pause and
  destroy.
- Cancellation callbacks preserve `ESP_ERR_INVALID_STATE`; the bus app ignores
  that stale lifecycle result after Back instead of showing a global fetch error.

## Verification

```text
git diff --check = pass
idf.py build = pass
application bytes = 0x2b62d0
application partition free = 46%
device cancellation capture = pass

The device was tested twice by opening a KMB stop request and pressing Back
before completion. Both runs showed owner cancellation with `count=1`, a
framework callback with `ESP_ERR_INVALID_STATE`, `cancelled=1`, one response
release, and no retry attempts. The app logged `Ignoring stale cancelled bus
request`, with no global fetch error after leaving the page.
```

The framework may still reach TLS/body completion after cancellation when the
cancel races an already-started connection. Its final result is discarded and
the response is released exactly once, which is the observed behavior here.
