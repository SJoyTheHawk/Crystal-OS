# Phase 3 Step 4 — KMB parser integration

**Status:** Passed on source, build, and device run.
**Plan:** [Phase 3 KMB catalog plan](crystal-http-phase3-kmb-catalog.md)

The owned KMB response body is now parsed before handoff cleanup. The parser
extracts the existing `data` array, route records, and KMB variants while
retaining the bounded PSRAM allocation and releasing the response exactly once.
Invalid JSON, missing data, transport errors, and empty results return a KMB
failure without dereferencing released response memory.

Verification: `git diff --check` and `idf.py build` pass. Device validation
passed with 1,601 KMB routes and variants, 406 CTB routes, a complete cache of
1,051 routes, and successful route-stop lookups for route 102 in both bounds.

The response was released once before handoff parsing, and the parser completed
from the owned copy. Invalid-response coverage remains part of the later
failure/lifetime gate.
