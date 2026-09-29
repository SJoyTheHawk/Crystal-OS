# Phase 3 Step 6 — Framework-owned KMB retries

**Status:** Passed on source, build, and forced-failure device validation.
**Plan:** [Phase 3 KMB catalog plan](crystal-http-phase3-kmb-catalog.md)

The KMB catalog operation submits exactly one `crystal_http_get()` request.
There is no KMB-local retry loop. The framework owns the configured ten
attempts, 500 ms initial backoff, 8 second backoff cap, timeout, and body limit.
The existing local retry loop remains only in the unchanged CTB direct-client
path.

`git diff --check` and the source inspection pass. The device gate requires a
forced KMB transport failure showing framework attempts and backoff, one final
bus result, and no multiplied attempts. A successful KMB fetch must still
produce one catalog result.

The supplied successful run confirms the success half of the gate: one KMB
framework request (`id=2`), one attempt (`attempt=1/10`), one callback, one
response release, one handoff, and one complete catalog result (`2/2`). KMB
resolved 1,600 routes and CTB resolved 407 routes. The KMB transfer took about
95 seconds but completed successfully; this is a slow network read, not a
nested retry.

The first forced-failure run exposed that an empty connection timeout was being
reported as `status=-1` with `error=ESP_OK`. That prevented the framework retry
classifier from retrying. `crystal_http` now normalizes a no-status response
with no transport error to `ESP_FAIL`; the forced-failure run must be repeated
and should then show attempts through `10/10` (or the configured final attempt).

The repeated forced-failure run passed: one framework request reached attempts
`1/10` through `10/10`, recorded `retries=9` and `failures=1`, delivered one
final callback with `ESP_ERR_HTTP_CONNECT`, released the response once, and
continued to CTB. CTB succeeded and the catalog emitted one partial result with
`1/2` providers. A subsequent normal run also completed with `2/2` providers.

For the controlled failure run, configure the component with
`-DCRYSTAL_HTTP_PHASE3_KMB_FORCE_FAILURE=ON` when regenerating the build. This
compile-time hook selects an invalid KMB endpoint only for that validation
image; it is disabled by default and must be turned off before the normal image
is flashed.
