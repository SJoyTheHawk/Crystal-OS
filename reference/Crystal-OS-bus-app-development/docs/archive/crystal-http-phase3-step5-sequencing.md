# Phase 3 Step 5 — Complete catalog sequencing

**Status:** Passed on source, build, and device run.
**Plan:** [Phase 3 KMB catalog plan](crystal-http-phase3-kmb-catalog.md)

The migrated KMB result is now part of the existing catalog sequence. The bus
worker resets catalog state once, waits for the bounded KMB handoff, publishes
the KMB download and resolve progress, then runs the unchanged CTB fetch. The
existing complete-cache and partial-cache decisions use both provider counts.

`git diff --check` and `idf.py build` pass. Device validation passed with the
expected KMB handoff and resolve sequence, CTB following KMB, a complete cache
of 1,051 routes, and `2/2` providers succeeding. The KMB transfer was slow
(about 64 seconds) but completed with HTTP 200 and no retry or lifetime error;
the backlight messages show the device remained active while the read was in
progress.

The KMB-success/CTB-failure partial-cache case remains part of the later
failure matrix.
