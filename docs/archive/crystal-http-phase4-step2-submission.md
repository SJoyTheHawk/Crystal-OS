# Phase 4 Step 2 — KMB Stop Framework Submission

**Status:** Source/build gate passed; normal inbound/outbound diagnostic capture passed; controlled failure pending.
**Plan:** [Phase 4 KMB route-stop plan](crystal-http-phase4-kmb-stops.md)

## Change recorded

KMB route-stop requests now submit through `crystal_http` with a dedicated
`KMB_STOPS_OWNER_ID`. The Step 2 request policy is:

```text
timeout: 8 seconds
maximum attempts: 3
initial backoff: 500 ms
backoff cap: 2 seconds
keep-alive: disabled
body limit: 64 KiB
```

The diagnostic callback records status, body length, attempts, and transport
error, releases the framework response once, and signals the handoff context.
The KMB stop parser and stop-list event delivery are intentionally not connected
in this step; that is the Step 4 gate. CTB stop requests remain on the existing
direct path.

The temporary failure validation option is not enabled in the normal build. It
will be added only when the controlled Step 2 failure capture is run and must
be removed before Step 9 acceptance.

## Verification

```text
git diff --check = pass
ESP-IDF = 6.1
idf.py build = pass
application bytes = 2841568 (0x2b5be0)
application partition free = 46%
firmware SHA-256 = f6f153b3a2bb5d3fcd695975909ca887cc9643843ef41ae1c745416c7b085033
sdkconfig SHA-256 = dae74a630f1bc45982785032205a178cb15f9ba55fe8e3e2b0ae29754a51cf0d
device diagnostic run = passed for inbound and outbound; forced failure = pending
```

Expected diagnostic output after flashing this image is:

```text
bus_service: KMB stops submitted route=102 bound=I bus_id=... framework_id=...
crystal_http: request attempt id=... attempt=1/3
crystal_http: response completed id=... status=200
bus_service: KMB stops diagnostic ... status=200 body=... attempts=1 error=ESP_OK
crystal_http: response released id=...
```

The supplied diagnostic run passed both directions: inbound returned HTTP 200
with 2,942 bytes in one attempt, and outbound returned HTTP 200 with 2,690
bytes in one attempt. Each response was released once. The stop parser was
still pending in that image, which explains the loading page.

## Gate result

Step 2 source/build and normal diagnostic checks pass. A controlled transport
failure remains to be recorded, but the successful diagnostic evidence is
enough to proceed with the handoff/parser work.
