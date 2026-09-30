# Phase 3 Step 2 — KMB framework submission

**Status:** Source/build gate passed; device gate pending.
**Plan:** [Phase 3 KMB catalog plan](crystal-http-phase3-kmb-catalog.md)

## Change recorded

The KMB catalog branch now submits its request through `crystal_http_get()`.
The callback is diagnostic-only: it logs the bus request id, framework request
id, HTTP status, body length, attempts, and transport error, then releases the
response exactly once. It does not parse the body, publish UI events, or hand
data to the catalog parser. CTB still uses the existing direct helper.

The initial policy is explicit:

```text
timeout: 15 seconds
max attempts: 10
backoff: 500 ms, capped at 8 seconds
keep-alive: disabled
body limit: 524288 bytes
owner: 0x4B4D4243 (KMBC)
```

The handoff context remains alive until the framework callback and is freed by
the callback, including the queue-rejection path. A missing response is logged
and does not dereference a null pointer.

## Verification

```text
git diff --check = pass
ESP-IDF = 6.1
idf.py build = pass
application bytes = 2836688 (0x2b4850)
application partition free = 46%
firmware SHA-256 = 9e7fd24ae5e4c1585d56f8a5e2c01ef6bb63f59c45d924ba66bf905c315adb70
sdkconfig SHA-256 = dae74a630f1bc45982785032205a178cb15f9ba55fe8e3e2b0ae29754a51cf0d
device flash/run = pending
```

The device serial port `/dev/cu.usbmodem101` is currently held by a Python
process, so this image has not been flashed and no device request IDs or
callback results are claimed here.

## Expected device evidence

After flashing this image and triggering a catalog refresh, the log should
contain matching lines like:

```text
bus_service: KMB catalog diagnostic queued bus_id=<bus> framework_id=<framework>
crystal_http: request attempt id=<framework> attempt=1/10
bus_service: KMB catalog diagnostic bus_id=<bus> framework_id=<framework> status=<...> body=<...> attempts=<...> error=<...>
```

The KMB body must not produce KMB route or variant counts at this step. The
callback must release the response once. CTB should continue through its
existing direct-client path.

## Gate result

The source and build portions pass. The device gate remains pending until this
image is flashed and a successful request plus a forced transport failure show
the submission, attempts, callback, and single response release without a
crash. Do not proceed to Step 3 until those records are captured.
