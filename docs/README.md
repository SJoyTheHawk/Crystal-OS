# Crystal OS documentation

## Current work

Current development targets the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B.
As of November 15, 2026, board bring-up, P4.2 visual/interaction acceptance,
and P4.3 network expansion pass for their documented scopes. P4.4 performance,
soak, package/OTA, and remaining feature qualification are still open. Phase 17
Lua runtime implementation has resumed and is in place; its device acceptance
checks and Bus script migration are tracked separately. Bus Lite 0.2.23 has
completed Phase 3 persistence acceptance; Phase 4 refresh and polish remain open.

- [P4.1 validation and build guide](ESP32_P4_PHASE1_VALIDATION.md): tested
  rev1.3 board, 720 × 720 display, signed builds, flash layout, and startup.
- [P4.2 visual/interaction acceptance](ESP32_P4_PHASE2_VALIDATION.md): boot,
  cards, Quick Settings, immediate Settings navigation, Wi-Fi UI, and keyboard.
- [P4.3 network acceptance](ESP32_P4_PHASE3_VALIDATION.md): Ethernet, Wi-Fi
  fallback, Weather/SNTP/HTTPS, pairing, and status indicators.

The established local root configuration is P4. Fresh checkouts must explicitly
select `esp32p4`; `sdkconfig` is ignored and the root `sdkconfig.defaults` still
preserves S3 defaults. The completed S3 baseline remains on branch
`ESP32-S3-Touch-LCD-4B` at commit `4c532a0`. Phases 14–16 have completed
development acceptance; this does not imply full P4 feature qualification.

Both user-supplied migration plans are retained:

- [Migration overview](MIGRATION_PLAN_ESP32_P4.md)
- [Detailed migration proposal](CRYSTAL_OS_V2_P4_MIGRATION_PLAN.md)

The plans retain proposed later work; their reconciliation sections link the
verified Phase 1 profile and distinguish pending checks. Their original
2025-01-13 dates are retained. They include older stack assumptions (IDF 5.3 as
the S3 baseline and IDF 5.5 as a proposed target); the completed S3 build uses
IDF 6.1. Verify board, BSP and dependencies before applying configuration
snippets. The branch decision and work order above replace older branching
instructions in those documents.

## Original plans and current contracts

- [v2 overview](APP_PLATFORM_V2_OVERVIEW.md)
- [v2 implementation plan](APP_PLATFORM_V2_IMPLEMENTATION_PLAN.md): original
  architecture and Phase 14–17 roadmap.
- [v2 design guide](APP_PLATFORM_V2_DESIGN_GUIDE.md)
- [v2 code guide](APP_PLATFORM_V2_CODE_GUIDE.md)
- [Original v1 plans](v1_implementation_plan/): preserved OS implementation plan,
  system design, code guide and validation checklist.
- [Catalog format](APP_CATALOG_FORMAT.md)

## Implementation and practical checks

- [Phase 14 implementation](PHASE_14_IMPLEMENTATION.md)
- [Phase 15/16 implementation and UI contract](APP_PLATFORM_V2_PHASE_15_16_IMPLEMENTATION.md)
- [Phase 15/16 closure](APP_PLATFORM_V2_PHASE_15_16_CLOSURE_CHECKLIST.md)
- [Phase 17 runtime implementation and validation](APP_PLATFORM_V2_PHASE_17_IMPLEMENTATION.md)
- [Phase 17 step-by-step P4 board test guide](APP_PLATFORM_V2_PHASE_17_TEST_GUIDE.md)
- [Phase 17.3 Bus Lua comparison and remaining acceptance plan](APP_PLATFORM_V2_PHASE_17_3_BUS_MIGRATION_PLAN.md)
- [Focused regression guide](APP_PLATFORM_V2_PHASE_15_16_TEST_GUIDE.md)
- [Bus transformation status](README_BUS_TRANSFORMATION.md), [Phase 3 results](BUS_APP_PHASE_3_RESULTS.md), and [installation guide](BUS_APP_QUICK_START.md)
- [Standard-service catalog policy and reproduction](BUS_STANDARD_SERVICE_CATALOG.md)
- [Wi-Fi recovery diagnosis and pending board checks](WIFI_RECOVERY_DIAGNOSIS.md)
- [Validation summary](test-evidence/phase15-16/2026-11-13-implementation-validation.txt)
- [ESP-IDF commands](ESP_IDF_COMMAND_GUIDE.md): existing S3 instructions.
- [Host tools](../tools/README.md) and [simulator status](SIMULATOR.md)

## Important findings

Preserve these during migration; do not assume a new board resolves them:

- [Bus architecture lessons](report/APP_PLATFORM_BUS_LESSONS.md) and
  [validation summary](report/BUS_APP_VALIDATION_SUMMARY.md)
- [Brookesia version strategy](report/BROOKESIA_VERSION_STRATEGY.md): retained
  rationale for the local framework; verify version claims before changing it.
- [Startup HTTPS sequencing](report/STARTUP_HTTPS_SEQUENCING_EXPERIMENT.md)
- [Watchdog investigation](report/WATCHDOG_TIMEOUT_MITIGATION.md)
- [S3 baseline notes](report/NO_BUS_BASELINE.md): historical branch/build context.
- [Bus example and known limitations](../examples/bus_app/README.md)

## Historical material

The [archive index](archive/README.md) covers completed reports, superseded
proposals and raw evidence. These are references, not a new work queue.
Exact duplicate Bus/HTTP notes have one retained copy under the
[Bus example](../examples/bus_app/docs/).
