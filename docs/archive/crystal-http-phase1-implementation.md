# Crystal OS Global HTTPS/TLS — Phase 1 Implementation Guide

**Status:** Phase 1 complete; acceptance passed on device
**Parent phase:** Incremental Global HTTPS/TLS plan  
**Scope:** Build and validate the shared `crystal_http` transport foundation. Bus and weather migration are deferred to later phases.

This phase must be implemented as a sequence of small commits. Every checkpoint has a visible device result. If a checkpoint fails, stop there, keep the last working commit, and diagnose that checkpoint before continuing.

## Phase 1.0 — Confirm the baseline

Start from the committed bus-app state.

Record:

- Git commit and working-tree status.
- `build/crystal_os.bin` size.
- ESP32-S3 internal heap and largest internal free block after boot.
- Free PSRAM after boot.
- One existing HTTPS result from the device, including elapsed time and failure type.

Do not change source or sdkconfig in this checkpoint.

Expected record:

```text
crystal_http baseline: commit=<sha>
firmware bytes=<size>
heap internal=<bytes> largest_internal=<bytes> psram=<bytes>
existing HTTPS result=<success|failure>
```

## Phase 1.1 — Add the empty framework component

Create the OS-owned component:

```text
components/crystal_http/
  CMakeLists.txt
  include/crystal_http.h
  src/crystal_http.c
```

Add only the component registration, public header, and an initialization function. Do not create an HTTP client yet.

The initial public functions are:

```c
bool crystal_http_init(void);
bool crystal_http_is_ready(void);
```

Make `crystal_core` depend on `crystal_http`. Do not change `bus_service` or weather networking.

Visible result:

```text
crystal_http: component initialized
```

Acceptance:

- Firmware builds.
- Device boots normally.
- Existing bus and weather behavior is unchanged.
- No transport task or queue is created yet.

## Phase 1.2 — Create the worker and queue only

Add one framework worker task and a small bounded request queue. The worker should start and wait without making network requests.

Use one worker task pinned to Core 0. The task stack size starts at 8192 bytes and will be measured later.

Visible result:

```text
crystal_http: worker started core=0
crystal_http: queue depth=8
```

Acceptance:

- The worker starts once.
- Repeated initialization does not create another worker.
- Boot remains stable with Wi-Fi starting normally.
- No HTTPS calls are made.

## Phase 1.3 — Add request submission without TLS

Add the request and response types to `crystal_http.h`:

```c
typedef struct {
    const char *url;
    uint32_t timeout_ms;
    uint8_t max_attempts;
    uint32_t retry_backoff_ms;
    uint32_t retry_backoff_max_ms;
    size_t max_body_bytes;
    bool keep_alive;
    uint32_t owner_id;
} crystal_http_options_t;

typedef struct {
    uint32_t request_id;
    int status_code;
    esp_err_t transport_error;
    uint8_t *body;
    size_t body_len;
    uint8_t attempts;
    uint32_t elapsed_ms;
} crystal_http_response_t;

typedef void (*crystal_http_callback_t)(
    const crystal_http_response_t *response,
    void *context);
```

Add:

```c
uint32_t crystal_http_get(const crystal_http_options_t *options,
                          crystal_http_callback_t callback,
                          void *context);
bool crystal_http_cancel(uint32_t request_id);
size_t crystal_http_cancel_owner(uint32_t owner_id);
void crystal_http_response_release(const crystal_http_response_t *response);
```

At this checkpoint, submit a synthetic request with an invalid URL or a deliberate “transport not enabled” result. This validates request IDs, queueing, callback delivery, and cleanup without introducing TLS variables yet.

Visible result:

```text
crystal_http: request queued id=1 owner=framework
crystal_http: request callback id=1 error=...
crystal_http: response released id=1
```

Acceptance:

- Queue-full behavior returns request id `0`.
- Every accepted request produces exactly one callback.
- Cancellation suppresses retry and produces one cancellation result.
- No response memory is leaked.

## Phase 1.4 — Add one real HTTPS smoke request

Add a temporary smoke request from `crystal_core` after `CRYSTAL_NETWORK_CONNECTED`. It must use the new transport and must not use the existing weather or bus HTTP helpers.

Use a small existing HTTPS JSON endpoint. The smoke callback only logs status/body length and releases the response. It must not call LVGL.

Configure the first request with:

```text
timeout: 15 seconds
attempts: 1
max body: 8 KB
keep-alive: disabled
owner: framework smoke test
```

Visible success result:

```text
crystal_http: request queued id=1 owner=framework-smoke
crystal_http: attempt id=1 number=1/1
crystal_http: TLS connected id=1
crystal_http: response id=1 status=200 body=<n> bytes
crystal_http: completed id=1 attempts=1 elapsed=<n> ms
crystal_core: HTTPS smoke test passed
```

Visible failure result:

```text
crystal_http: request id=1 transport failure=<error>
crystal_core: HTTPS smoke test failed
```

Acceptance:

- The device remains responsive after success and failure.
- Certificate validation remains enabled.
- No direct app HTTP helper is involved.
- The response body is allocated in PSRAM and released after the callback.

## Phase 1.5 — Add bounded response buffering

Implement the real body path using `open`, `fetch_headers`, `read`, and `close`.

Rules:

- Allocate response storage from PSRAM.
- Reject a declared content length above `max_body_bytes`.
- Support unknown/chunked content lengths with bounded growth.
- Always NUL-terminate an allocated body when there is room.
- Return status code and transport error separately.
- Never expose a pointer after `crystal_http_response_release()`.
- Clean up the client on every path: open failure, header failure, status failure, allocation failure, read failure, cancellation, and success.

Visible result:

```text
crystal_http: body allocation id=1 bytes=<n> memory=PSRAM
crystal_http: body received id=1 bytes=<n>
crystal_http: response released id=1
```

Acceptance:

- Small response succeeds.
- Empty response is reported without dereferencing a null body.
- Oversized response fails without allocating beyond the configured limit.
- Repeated requests return the heap to the baseline watermark.

## Phase 1.6 — Add retry and cancellation behavior

Implement request-specific retry settings. Do not introduce a global fixed retry policy.

Retry transport failures, TLS handshake failures, DNS failures, connection timeouts, socket read failures, HTTP 408, 425, 429, and 5xx responses. Do not retry ordinary 4xx responses.

The current implementation clamps attempts to ten, uses a default backoff of 500 ms,
caps the delay at 8 seconds, and logs each attempt, retry delay, completion, and
cancellation request. Cancellation is checked before an attempt, while reading a
body, and during backoff; a cancelled request is delivered once with
`ESP_ERR_INVALID_STATE` and is never retried.

Use bounded backoff:

```text
delay = min(retry_backoff_ms * 2^(attempt - 1), retry_backoff_max_ms)
```

Cancellation rules:

- Queued requests are removed without opening a socket.
- In-flight requests are marked cancelled and stop after the current client operation or read chunk.
- A cancelled request never retries.
- Owner cancellation cancels all queued requests belonging to that owner.

Visible result:

```text
crystal_http: request id=1 attempt=1/3 failed error=...
crystal_http: request id=1 retry in=500 ms
crystal_http: request id=1 cancelled owner=framework-smoke
```

Acceptance:

- A forced connection failure produces the expected number of attempts.
- A successful retry produces one final success callback.
- Cancellation prevents later attempts.
- No callback references freed request context.

## Phase 1.7 — Add diagnostics and memory measurements

Add framework counters and heap diagnostics:

- queued requests
- completed requests
- successful requests
- transport failures
- HTTP failures
- retry attempts
- cancellations
- current queue depth
- peak response body size
- internal heap before and after TLS
- largest internal free block before and after TLS
- PSRAM before and after TLS

Visible result:

```text
crystal_http: heap before internal=<n> largest_internal=<n> psram=<n>
crystal_http: heap peak internal=<n> psram=<n>
crystal_http: heap after internal=<n> largest_internal=<n> psram=<n>
crystal_http: stats requests=<n> successes=<n> retries=<n> failures=<n> cancelled=<n>
```

Acceptance:

- Repeated smoke requests show no progressive heap loss.
- The body allocation is visible in PSRAM.
- TLS failures identify whether the failure occurred during DNS, connect, handshake, headers, or body read.

The current implementation logs internal heap, largest internal block, and PSRAM
before and after each real request. It also records queue submissions, completions,
successes, transport and HTTP failures, retry attempts, cancellations, current queue
depth, and peak response body size. The completion log prints a compact cumulative
counter line so the result is visible without a separate diagnostic caller.

## Phase 1.8 — Finish the checkpoint and commit

Run the complete Phase 1 test set:

1. Wi-Fi already connected, smoke request succeeds.
2. Wi-Fi disconnected, request is rejected or deferred cleanly.
3. TLS/connect timeout retries and fails cleanly.
4. HTTP error returns status without being treated as a transport success.
5. Oversized response is rejected.
6. Request cancellation works before connection and during retry.
7. Owner cancellation removes multiple requests.
8. Repeated requests do not reduce the heap baseline.
9. Existing bus route catalog and stop behavior remain unchanged.
10. Device remains responsive during all failures.

Run:

```text
git diff --check
idf.py build
idf.py size-components
```

Commit only after the logs and measurements are captured. The commit should contain the new framework component, its dependency wiring, the temporary smoke-test integration, and the Phase 1 diagnostics. Bus and weather HTTP migration must remain outside this commit.

## Rollback points

Create or retain a usable commit after each checkpoint:

- `phase-1.1-empty-component`
- `phase-1.2-worker`
- `phase-1.3-request-lifecycle`
- `phase-1.4-https-smoke`
- `phase-1.5-response-buffer`
- `phase-1.6-retry-cancel`
- `phase-1.7-diagnostics`
- `phase-1-complete`

If the device becomes unstable, revert to the last checkpoint whose visible output passed. Do not migrate bus or weather until `phase-1-complete` is accepted.
