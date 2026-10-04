# Phase 5 Step 7 - CTB Stops Slice 1 Record

**Status:** Slice 1 implementation signed off on 2026-10-01; formal device
acceptance remains conditional until the flashed firmware commit and the
remaining Step 0 route-catalog capture are recorded.

**Plan:** [Phase 5 bus service code guide](bus-app-next-phase-code-guide.md)

**Sign-off:** Steps 0 through 7 have a recorded source/build or device result,
the normal test switch is disabled, and the CTB stop ownership boundary is
ready for the tracked Slice 2 work. The conditional acceptance items above are
documentation gaps, not an instruction to change the Slice 1 implementation.

## Step 6 source boundary

The CTB route-stop branch in `process_stops_request()` constructs the CTB URL
and submits it through `submit_ctb_stops()`. That helper calls
`crystal_http_get()` with `CTB_STOPS_OWNER_ID`; the callback copies the bounded
body, releases the framework response, and the bus worker parses the handoff.
The CTB stop branch contains no direct `esp_http_client`, `esp_tls`,
`esp_crt_bundle`, or `http_get_json()` call.

The remaining direct helper is intentionally still used by unrelated catalog
and ETA operations. Those paths remain in later migration slices.

```text
CTB stop direct-client calls = 0
CTB stop submission through crystal_http = pass
git diff --check = pass
idf.py build = pass
CRYSTAL_HTTP_PHASE5_CTB_STOPS_FORCE_FAILURE = OFF
```

## Device evidence

The supplied capture used route 18 and exercised both directions, cancellation,
stale selection, KMB cancellation, and Wi-Fi cancellation.

```text
phase5 slice=1
firmware commit=unrecorded; flashed image included uncommitted workspace changes

operator=CTB route=18 bound=I
bus request=1 framework request=2
status=200 error=ESP_OK attempts=2 body=3278 stops=27
response releases=1
heap before/after internal=28191/21091 largest=12288/7680 psram=4279628/4276296
result=pass

operator=CTB route=18 bound=O
bus request=2 framework request=3
status=200 error=ESP_OK attempts=1 body=2806 stops=23
response releases=1; result arrived after Back and was ignored as stale
heap before/after internal=28575/27567 largest=7680/7680 psram=4279628/4276808
result=pass

operator=CTB route=18 bound=I
bus request=3 framework request=4
status=200 error=ESP_ERR_INVALID_STATE attempts=1 body=0 stops=0
response releases=1; stale cancellation ignored by app
heap before/after internal=28367/28155 largest=7680/7680 psram=4279628/4279628
result=pass

operator=KMB route=18 bound=O
bus request=4 framework request=5
status=200 error=ESP_ERR_INVALID_STATE attempts=1 body=0 stops=0
response releases=1; stale cancellation ignored by app
result=pass

operator=CTB route=18 bound=I
bus request=6 framework request=7
status=0 error=ESP_ERR_INVALID_STATE attempts=1 body=0 stops=0
response releases=1; Wi-Fi cancellation ignored by app
result=pass
```

The capture also shows a successful CTB inbound request after cancellation,
and the app remained usable. No retry started after cancellation. The Quick
Settings memory guard was tested separately and no longer triggered the I2S
DMA abort during an active CTB TLS request.

## Handoff

Known follow-up work:

- Confirm and capture the CTB route-catalog response that associates each
  direction with its authoritative destination.
- Implement [Tracked Slice 2](crystal-http-phase5-slice2-ctb-route-variants-plan.md)
  for CTB inbound/outbound route variants and destinations; preserve
  `route + CTB + bound + service_type` identity.
- Return to the deferred KMB Phase 4 failure/lifetime matrix after the CTB and
  weather client migrations.

Formal slice acceptance remains deferred because the supplied device log does
not identify the flashed firmware commit and the Step 0 route-list network
capture is not recorded. The runtime CTB stop and lifecycle evidence above is
the baseline for the next slice.
