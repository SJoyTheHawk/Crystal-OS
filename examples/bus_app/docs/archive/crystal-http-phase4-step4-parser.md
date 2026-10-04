# Phase 4 Step 4 — KMB Stop Parser Integration

**Status:** Source/build gate passed; normal inbound/outbound parser validation passed; failure validation pending.
**Plan:** [Phase 4 KMB route-stop plan](crystal-http-phase4-kmb-stops.md)

The KMB response body is parsed from the handoff-owned PSRAM copy before the
handoff is cleaned up. The existing stop normalization and sequence handling
are preserved, and successful results still arrive as `BUS_EVT_STOPS_LIST`
with the original bus request id. The UI's stale `stop_request_id_` check
therefore remains effective.

The parser now validates that `stop` is a JSON string and accepts the provider's
`seq` value as either a numeric JSON value or a numeric string. It rejects
malformed or empty data without committing rows, and posts a controlled
`BUS_EVT_ERROR` on transport, HTTP, parse, allocation, or empty-data failure.
The framework response is released once and the copied body is freed once.

Validation completed:

```text
git diff --check = pass
idf.py build = pass
device inbound stop rows = passed: 34
device outbound stop rows = passed: 31
device failure and stale-request checks = pending
```

The supplied run showed `KMB stops resolved route=102 count=34` and
`count=31`, followed by `bus_app: Found 34 stops` and `Found 31 stops`. The
normal parser/UI path is therefore passed. Failure, cancellation, and stale
request checks remain before the full Step 4 gate is closed.
