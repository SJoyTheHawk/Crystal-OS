# Incremental Global HTTPS/TLS Implementation

Each phase ends with a separate commit and a device test. A phase is accepted only when its visible output matches the expected result.

## Phase 0 — Baseline checkpoint

**Purpose:** Freeze the current working behavior before changing the networking stack.

- Commit the current bus app and TLS-related code.
- Record the current ESP32-S3 firmware size.
- Record free internal heap, largest internal heap block, free PSRAM, and route-stop request timing.
- Capture baseline logs for:
  - Wi-Fi connected
  - KMB route catalog request
  - CTB route catalog request
  - KMB route-stop request
  - TLS timeout or handshake failure

**Visible result:**

```text
TLS baseline:
internal heap: ...
largest internal block: ...
PSRAM free: ...
bus route-stop result: success/failure
```

This phase changes no code.

## Phase 1 — Add the `crystal_http` framework component

**Purpose:** Create the shared transport without migrating existing clients.

Add the new OS-owned component with:

- Request queue.
- One worker task.
- One active HTTPS connection at a time.
- Strict certificate-bundle validation.
- PSRAM response buffering.
- Request ids and owner ids.
- Request-specific timeout, retry, backoff, body limit, and keep-alive options.
- Cancellation by request and owner.
- Response release function.
- Structured transport counters.

No weather or bus code uses it yet.

Add a temporary framework smoke-test request triggered after Wi-Fi obtains an IP. It should request one small known HTTPS JSON endpoint and print the result.

**Visible result:**

```text
crystal_http: worker started
crystal_http: request id=1 owner=framework attempt=1/1
crystal_http: TLS connected
crystal_http: response status=200 body=... bytes
crystal_http: request complete elapsed=... ms
```

Failure output must include the transport error and HTTP status without crashing.

## Phase 2 — Validate TLS memory behavior

Detailed procedure: [crystal-http-phase2-validation.md](crystal-http-phase2-validation.md)

**Status:** Passed on device. Twenty cold-start HTTPS smoke requests completed successfully with stable cleanup measurements.

**Purpose:** Measure whether PSRAM-backed TLS allocation improves reliability before migrating applications.

Run repeated smoke-test requests while logging:

- Internal heap before TLS.
- Largest internal block before TLS.
- PSRAM free before TLS.
- Internal and PSRAM values during the connection.
- TLS result and elapsed time.
- Peak response size.

Evaluate the existing settings:

- `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`
- TLS record buffer sizes.
- ESP-TLS dynamic buffer support.
- Internal heap reserve.
- PSRAM allocation thresholds.

Only change sdkconfig values when measurements show a need. Do not enlarge buffers blindly.

**Visible result:**

```text
crystal_http: heap before internal=... largest=... psram=...
crystal_http: tls peak internal=... psram=...
crystal_http: success attempts=... elapsed=...
```

Acceptance requires repeated requests without memory leaks or progressive heap loss.

## Phase 3 — Migrate the KMB route catalog

Detailed implementation plan: [crystal-http-phase3-kmb-catalog.md](crystal-http-phase3-kmb-catalog.md)

**Purpose:** Move the first real bus operation to the framework.

Change only the KMB route catalog request in `bus_service`.

- Submit the request through `crystal_http`.
- Preserve the existing catalog progress events.
- Preserve PSRAM parsing and filesystem caching.
- Keep CTB on the old path temporarily.
- Remove direct HTTP usage only from the KMB catalog branch.

**Visible result:**

```text
bus_service: KMB request submitted to crystal_http id=...
crystal_http: request id=... attempt=...
crystal_http: response status=200 body=... bytes
bus_service: KMB data downloaded
bus_service: KMB data resolved
bus_service: KMB data saved
```

Test both successful fetch and forced KMB connection failure.

## Phase 4 — Migrate KMB route-stop requests

**Purpose:** Directly address the unstable request currently failing on the ESP32.

Move only KMB route-stop requests to `crystal_http`.

- Keep the corrected URL mapping:
  - `I` → `inbound`
  - `O` → `outbound`
- Use an interactive request policy with the configured timeout and retries.
- Preserve stop-list event delivery and PSRAM ownership.
- Preserve cancellation and stale-request checks.
- Ensure failed responses never free error text as a stop pointer.

**Visible result:**

```text
bus_service: KMB stops submitted route=102 bound=I request=...
crystal_http: request id=... attempt=1/10
crystal_http: TLS failure ... retrying
crystal_http: request id=... attempt=2/10
crystal_http: response status=200 body=... bytes
bus_service: found 34 stops
```

The stop page must show the stop rows after a successful response. Failure must show an error and leave the app usable.

## Phase 5 — Migrate remaining bus HTTPS operations

**Purpose:** Complete the bus service migration in small functional groups.

Migrate in this order:

1. KMB route variants.
2. KMB ETA requests.
3. CTB route catalog.
4. CTB route variants and stop requests when their normalization path is ready.
5. CTB ETA requests.

After each operation, retain the existing bus event type and UI behavior.

**Visible result for each operation:**

```text
bus_service: operation submitted to crystal_http
crystal_http: request started
crystal_http: response status=...
bus_service: operation parsed successfully
```

No bus source file should directly include or call `esp_http_client` after this phase.

## Phase 6 — Migrate Crystal OS weather networking

**Purpose:** Make weather the second framework client.

Move weather location and forecast requests from `crystal_core`’s private HTTP helper to `crystal_http`.

- Preserve existing weather retry pacing.
- Use a smaller response limit.
- Parse responses in the core service path.
- Continue delivering readings through the existing Crystal OS UI event queue.
- Remove the static weather response buffer and direct HTTP client ownership.

**Visible result:**

```text
crystal_core: weather request submitted to crystal_http
crystal_http: response status=200 body=... bytes
crystal_core: weather parsed successfully
crystal_core: weather event posted
```

Test automatic refresh, manual refresh, Wi-Fi loss, and failed TLS connections.

## Phase 7 — Global ownership enforcement and diagnostics

**Purpose:** Confirm the framework is truly the single HTTPS owner.

- Remove duplicate HTTP helpers and direct TLS includes from weather and bus code.
- Add a repository check that flags `esp_http_client`, `esp_tls`, and certificate-bundle usage outside `crystal_http`.
- Expose framework counters for diagnostics.
- Add logs for queue depth, cancellations, retries, transport failures, HTTP failures, and peak body size.
- Verify owner cancellation when bus app and weather app pause or destroy.
- Verify late callbacks are ignored safely.

**Visible result:**

```text
crystal_http: active=0 queued=0
crystal_http: requests=...
crystal_http: retries=...
crystal_http: successes=...
crystal_http: transport_failures=...
crystal_http: cancellations=...
crystal_http: peak_body=... bytes
```

## Phase 8 — Final reliability validation

Run repeated device tests with separate results recorded for each case:

- Normal KMB route-stop fetch.
- Normal CTB fetch.
- TLS handshake failure.
- TCP connection timeout.
- DNS failure.
- HTTP 422.
- HTTP 5xx.
- Wi-Fi disconnect during connection.
- Wi-Fi disconnect during body download.
- App pause during retry.
- App destroy during an in-flight request.
- Weather and bus requests competing for the transport.
- Repeated fetches checking for heap loss.

Final acceptance requires:

- One shared HTTPS implementation.
- No direct HTTP client ownership outside `crystal_http`.
- No invalid frees or stale UI writes.
- No persistent internal-heap or PSRAM leak.
- Correct cancellation and retry behavior.
- Successful KMB, CTB, and weather migrations.
- Measured TLS memory behavior documented in the repository.
