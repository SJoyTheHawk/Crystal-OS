# Phase 3 Step 8 — Failure and lifetime matrix

**Status:** Transport-failure row passed; remaining rows pending.
**Plan:** [Phase 3 KMB catalog plan](crystal-http-phase3-kmb-catalog.md)

This gate adds no runtime feature code. It validates that the integrated KMB
handoff reports failures consistently and releases its response and context
exactly once.

| Case | Setup | Expected evidence |
| --- | --- | --- |
| Transport failure | Build with `CRYSTAL_HTTP_PHASE3_KMB_FORCE_FAILURE=ON` | Framework reaches final attempt, one callback/release, CTB continues, partial result |
| Non-success status | Use a controlled endpoint returning HTTP 4xx/5xx | HTTP status is preserved and KMB is failed without parsing |
| Body limit | Return a body over 512 KiB | `ESP_ERR_INVALID_SIZE`, one release, no catalog commit |
| Invalid JSON | Return HTTP 200 with malformed JSON | Controlled KMB parse failure, no crash or stale variants |
| Pending callback timeout | Hold the response beyond the handoff wait | Timeout requests cancellation; late callback cleans up once |
| Cancellation/app pause | Cancel the catalog during retry or handoff | Cancellation is reported and no late data is committed |
| Stale callback | Tear down the catalog context before callback delivery | Callback is ignored or safely released with no use-after-free |

Already validated: the forced transport-failure case reached ten framework
attempts and produced one partial result, and normal success produced a complete
`2/2` catalog. The remaining rows require controlled device/server conditions;
do not mark Step 8 passed from a normal fetch alone.

The latest forced-failure capture confirms that row: KMB framework request `id=2`
made attempts `1/10` through `10/10`, recorded `retries=9` and `failures=1`,
returned `ESP_ERR_HTTP_CONNECT` with status `0`, and released the response once.
CTB then completed normally with 407 routes, and the catalog saved a partial
cache with `1/2` providers. The KMB body and variant count remained zero, so no
failed-provider data was committed.

## Network-loss cancellation improvement

The existing `CRYSTAL_NETWORK_DISCONNECTED` listener in `bus_app` now calls
`bus_service_network_disconnected()`. That operation marks the active catalog
cancelled, cancels the KMB framework owner, and closes the active CTB direct
client. Both catalog paths check the cancellation state before retrying, so a
Wi-Fi loss stops the current provider operation instead of waiting for the
full retry budget. A later connected event starts a fresh catalog request.

The firmware build passes. Device validation should show a cancellation log
near the disconnect event, no further KMB or CTB retry attempts for that
operation, one final catalog failure/partial result, and a new request after
Wi-Fi reconnects.

The first device capture passes the KMB cancellation portion: Wi-Fi loss was
reported at `24257 ms`, owner cancellation at `24265 ms`, the KMB callback
returned `ESP_ERR_INVALID_STATE` at `24463 ms`, and the response was released
once. CTB had not started before the disconnect, so an active CTB cancellation
still needs one separate capture. The duplicate disconnected notification from
the Wi-Fi disable/lost-IP sequence was harmless; both events only observed the
same cancellation state.

That capture also exposed a reconnect race: a cancelled operation could deliver
its final catalog event after the reconnect event and leave bootstrap marked as
checked. The catalog result now uses `ESP_ERR_INVALID_STATE` for network-loss
cancellation; when IP is already restored, `bus_app` clears the checked state
and lets the 500 ms bootstrap timer queue a fresh KMB+CTB request. The firmware
build passes. The device capture showed the remaining ordering bug: the
cancelled result arrived before reconnect and set `catalog_bootstrap_checked_`
again; the connected handler now clears that flag as well, so either event order
re-arms the refresh.

The follow-up device run validates the complete recovery sequence. After the
first cancellation, reconnect queued catalog request `bus_id=2` / framework
request `id=3`, which was cancelled by a second Wi-Fi loss. A later reconnect
queued `bus_id=3` / framework request `id=4`; KMB completed with 1,600 routes and
variants, CTB completed with 407 routes, and the cache completed with 1,052
routes and `2/2` providers. This confirms automatic re-fetch after reconnect.

For each run record the firmware commit, bus/framework request ids, status,
error, attempts, response-release count, provider result, and before/after
heap and PSRAM readings. Stop if any row shows a duplicate release, stale data,
or a callback after context cleanup.
