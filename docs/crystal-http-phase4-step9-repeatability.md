# Phase 4 Step 9 — Repeatability evidence

**Status:** Deferred final acceptance. Normal repeatability passed; the full
Step 8 matrix and final repeatability run will be completed after the next bus
integration slice.

Source: user device capture `1b3767e0-8905-44c7-b59c-7b94d7b9aabd`,
reviewed 2026-09-30. Workspace HEAD at review:
`d79730c740c52b679c161cfeb138507d18525820`, with uncommitted Phase 4 changes.
The capture does not identify the flashed firmware commit.

## Successful requests

Every request below returned HTTP 200 / ESP_OK on one attempt, with one
framework response release. Directions cover both inbound and outbound,
although the sequence does not strictly alternate on every request.

| Bus/framework id | Route | Bound | Body bytes | Resolved stops | Elapsed ms |
| --- | --- | --- | --- | --- | --- |
| 2/3 | 102 | I | 2942 | 34 | 833 |
| 3/4 | 102P | I | 2296 | 26 | 742 |
| 4/5 | 10 | O | 4070 | 48 | 1186 |
| 5/6 | 101 | O | 3026 | 35 | 1234 |
| 6/7 | 101 | I | 3026 | 35 | 778 |
| 7/8 | 106 | I | 4034 | 47 | 4300 |
| 8/9 | 106 | O | 4118 | 48 | 2205 |
| 9/10 | 106A | O | 1871 | 21 | 740 |
| 10/11 | 106P | I | 3061 | 35 | 1097 |
| 13/14 | 106 | O | 4118 | 48 | 1024 |

The app logs the displayed stop counts for all of these except bus request 7,
whose parser reports 47 stops but whose app display line is absent.

## Cancellation and reconnection

Bus/framework requests 11/12 and 12/13 were cancelled by Back. Each returned
ESP_ERR_INVALID_STATE, body=0, attempts=1, released once, and was ignored as
stale by the app. Request 13/14 subsequently displayed 48 stops successfully.
Final framework totals are 14 requests, 12 successes (including smoke and
catalog), two cancellations, zero failures, and zero retries.

Wi-Fi disconnected at 78496 ms after request 6/7 had completed. Reconnection
at 82021 ms reused the fresh catalog, and later stop requests succeeded. This
is reconnection evidence, not a new in-flight Wi-Fi cancellation test. Prior
forced-failure and in-flight Wi-Fi cancellation evidence remains in Step 8.

## Memory observations and limits

Pre-request PSRAM is 4501916 bytes initially and 4500812 bytes after audio
initialization. Subsequent visible pre-request samples remain at 4500812;
both cancelled requests release back to that value. Successful framework
release samples still include the service-owned handoff buffer; the next
request baseline provides evidence that it was subsequently freed.

Internal memory changes across audio initialization and Wi-Fi toggling, so
the whole run is not a fixed-workload leak measurement. Minimum logged TLS
internal heap is 21751 bytes; minimum largest block is 7680 bytes. All observed
requests complete without allocation failure, crash, or duplicate release.
This supports repeatability but does not prove long-term absence of leaks.

Normal bodies above 1 KiB confirm the temporary limit is inactive. No runtime
code change is required by this capture. This evidence is sufficient to
continue the bus integration work, but it is not a formal Phase 4 acceptance.
Re-run the final matrix against a recorded firmware commit before closing
Phase 4.
