# Phase 3 Step 7 — KMB direct-client removal

**Status:** Passed on source and build validation.
**Plan:** [Phase 3 KMB catalog plan](crystal-http-phase3-kmb-catalog.md)

The KMB catalog branch has one framework submission through `crystal_http_get()`
and no `esp_http_client` call. Its callback owns the framework response until it
copies the body into the KMB handoff context; the bus worker parses that owned
copy and releases the context once. The KMB submission and callback names now
identify the production handoff path rather than the earlier diagnostic stage.

The `esp_http_client` include and generic `http_get_json()` helper remain in
`bus_service.c` because CTB catalog, route variants, stops, and ETA operations
still use the direct client. This step deliberately does not change those paths.

Validation:

- Source inspection finds `esp_http_client` only in the shared helper; there is
  no direct client call in the KMB catalog submission or callback.
- `git diff --check` passes.
- A normal firmware build passes with the KMB failure hook disabled.
- The previously captured normal run completed with KMB and CTB providers,
  while the forced KMB failure run continued to CTB and produced the expected
  partial result. Those captures remain the device evidence for this unchanged
  behavior.

The latest normal-path capture confirms the same behavior on the Step 7 image:
framework request `id=2` completed in one attempt with HTTP 200 and a 349,510
byte body; the response was released once, KMB resolved 1,600 routes and
variants, CTB resolved 407 routes, and the cache completed with 1,052 routes
and `2/2` providers. Heap readings returned to the pre-request range after
release (`internal=58,151`, `PSRAM=4,375,008`), with no error or partial-cache
result.

The next gate is Step 8, the failure and lifetime matrix. Do not remove the
controlled failure hook before that validation is complete.
