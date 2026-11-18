# Bus Lite documentation

The P4 Lua package lives in [examples/bus3p](../../examples/bus3p/README.md).
Phases 1–4 are implemented; Phase 3 persistence has P4 acceptance. Follow
the [Phase 4 results](BUS_APP_PHASE_4_RESULTS.md) for the current package
checkpoint and remaining reliability checks. Older versions and resource
limits in historical reports describe their capture date.

## Current guides

- [Install and board test](BUS_APP_QUICK_START.md): firmware/package installation
  and phase-specific device checks. Check Phase 4 results for newer artifacts.
- [Lua code patterns](BUS_APP_LUA_CODE_PATTERNS.md): provider integration,
  persistence, UI and lifecycle contracts.
- [Transformation guide](BUS_APP_TRANSFORMATION_GUIDE.md): S3/Flutter reference
  behavior, architecture and implementation strategy.
- [Standard-service catalog](BUS_STANDARD_SERVICE_CATALOG.md): provider policy,
  source snapshots and reproduction.
- [Display flash investigation](BUS_APP_DISPLAY_FLASH_INVESTIGATION.md):
  observations, candidate cause and pending board confirmation.

## Platform and diagnostics

- [Lua runtime implementation](../platform/APP_PLATFORM_V2_PHASE_17_IMPLEMENTATION.md)
  and [code guide](../platform/APP_PLATFORM_V2_CODE_GUIDE.md)
- [Wi-Fi checks](../networking/WIFI_CHECKS.md)
- [Architecture lessons](../report/APP_PLATFORM_BUS_LESSONS.md) and
  [native Bus example](../../examples/bus_app/README.md)

## Historical results

The [Bus archive](../archive/bus/README.md) contains Phase 1–3 results and the
superseded Phase 17.3 migration plan, including original checkpoint hashes
and measurements. Use the current guides above for installation and ongoing work.

Return to the [documentation index](../README.md).
