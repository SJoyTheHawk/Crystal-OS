# Phase 3 Step 1 — KMB handoff context

**Status:** Source/build gate passed; device behavior gate not applicable. Step 0
device baseline remains pending.
**Plan:** [Phase 3 KMB catalog plan](crystal-http-phase3-kmb-catalog.md)

## Change recorded

Added an isolated `kmb_catalog_handoff_t` in
`components/bus_service/src/bus_service.c`. It contains:

- the bus request id;
- the KMB URL;
- the future `crystal_http` framework request id;
- completion, cancellation, and timeout state;
- a borrowed response pointer valid only during the future callback.

The bus worker is documented as the context owner. The create helper allocates
the context, and `kmb_catalog_handoff_cleanup()` is its single cleanup path.
The context is deliberately not allocated, submitted, or read by the current
catalog operation; the direct KMB/CTB request path is unchanged for Step 1.

The bus-service component now declares its existing dependency on
`crystal_http`, allowing the next step to submit through that component without
changing dependency resolution later.

## Verification

```text
git diff --check = pass
ESP-IDF = 6.1
idf.py build = pass
application bytes = 2836640 (0x2b4840)
application partition free = 46%
firmware SHA-256 = f26bad884aeecb637ccf277fc13ce046f01e38f69af41d726ed630a495e4ad6a
sdkconfig SHA-256 = dae74a630f1bc45982785032205a178cb15f9ba55fe8e3e2b0ae29754a51cf0d
device flash/run = not performed
```

The compiler reports the two new helpers as unused. That is expected while the
Step 1 requirement keeps them disconnected from the live request path; Step 2
will consume them when the KMB submission is migrated.

## Gate result

The source and build portions of Step 1 pass. No device run is required to
validate behavior because the request path is unchanged. Do not mark Step 0 or
the Phase 3 device acceptance complete until the three baseline runs in the
Step 0 record have been captured.
