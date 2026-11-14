# Crystal OS documentation

## Current work

Main is the P4 migration branch. The completed S3 baseline is preserved on
ESP32-S3-Touch-LCD-4B at commit 4c532a0. **Migrate to P4 first, then resume
Phase 17.** Phases 14–16 have completed development acceptance. Firmware is
configured for S3 by default. An isolated P4 Phase 1 profile now builds, flashes
and boots with Wi-Fi/HTTPS smoke tests passing; physical UI acceptance remains
open. See the [Phase 1 validation and build guide](ESP32_P4_PHASE1_VALIDATION.md).

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
  architecture and Phase 14–17 roadmap; resume Phase 17 after migration.
- [v2 design guide](APP_PLATFORM_V2_DESIGN_GUIDE.md)
- [v2 code guide](APP_PLATFORM_V2_CODE_GUIDE.md)
- [Original v1 plans](v1_implementation_plan/): preserved OS implementation plan,
  system design, code guide and validation checklist.
- [Catalog format](APP_CATALOG_FORMAT.md)

## Implementation and practical checks

- [Phase 14 implementation](PHASE_14_IMPLEMENTATION.md)
- [Phase 15/16 implementation and UI contract](APP_PLATFORM_V2_PHASE_15_16_IMPLEMENTATION.md)
- [Phase 15/16 closure](APP_PLATFORM_V2_PHASE_15_16_CLOSURE_CHECKLIST.md)
- [Focused regression guide](APP_PLATFORM_V2_PHASE_15_16_TEST_GUIDE.md)
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
