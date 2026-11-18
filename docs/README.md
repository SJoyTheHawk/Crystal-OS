# Crystal OS documentation

## Current work

Current development targets the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B.
P4.1–P4.3 are accepted for their documented scopes; P4.4 performance, soak,
package/OTA and remaining feature qualification stay open. Phase 17 runtime
and Bus Lite Phases 1–4 are implemented. Phase 3 persistence is accepted on
P4; remaining runtime and Phase 4 checks are tracked in the records below.

- [P4 build, flash and validation](hardware/esp32-p4/README.md)
- [Phase 17 implementation](platform/APP_PLATFORM_V2_PHASE_17_IMPLEMENTATION.md)
  and [board test guide](platform/APP_PLATFORM_V2_PHASE_17_TEST_GUIDE.md)
- [Bus Lite installation and tests](bus/BUS_APP_QUICK_START.md) and
  [current Phase 4 results](bus/BUS_APP_PHASE_4_RESULTS.md)
- [Wi-Fi checks](networking/WIFI_CHECKS.md): signal-loss recovery still needs
  hardware confirmation; earlier PM and socket-leak explanations are not
  verified fixes.

## Browse by topic

For application development, start with the [Lua app and CAPP reference](../APPLICATION_ABI.md):
writing Lua apps, using the app APIs, and packaging, signing and installing `.capp` files.

| Folder | Contents |
| --- | --- |
| [platform](platform/README.md) | v2 architecture, runtime, ABI, catalog, lifecycle and app-update plans |
| [bus](bus/README.md) | Lua Bus implementation, installation, catalog policy and active investigations |
| [hardware/esp32-p4](hardware/esp32-p4/README.md) | P4 migration plans, tested build commands and board validation |
| [networking](networking/README.md) | Current Wi-Fi diagnostics and incident analysis |
| [guides](guides/README.md) | Simulator, power policy and preserved S3 command guide |
| [report](report/README.md) | Framework, memory, watchdog and HTTPS findings |
| [reference](reference/esp32-p4-factory/README.md) | Preserved P4 factory baseline and source provenance |
| [test-evidence](test-evidence/) | Current validation summaries and captured board results |
| [images](images/README.md) | Screenshots, hardware photos and attribution |
| [archive](archive/README.md) | Original v1 plans, completed milestones and superseded investigations |

[Host tools](../tools/README.md) and the [Bus package](../examples/bus3p/README.md)
have their own usage guides alongside the source.

## Important findings

- [Bus architecture lessons](report/APP_PLATFORM_BUS_LESSONS.md) and
  [validation summary](report/BUS_APP_VALIDATION_SUMMARY.md)
- [Brookesia version strategy](report/BROOKESIA_VERSION_STRATEGY.md): retained
  rationale for the local framework; verify version claims before changes.
- [Startup HTTPS sequencing](report/STARTUP_HTTPS_SEQUENCING_EXPERIMENT.md)
  and [watchdog investigation](report/WATCHDOG_TIMEOUT_MITIGATION.md)
- [Wi-Fi and local panel incident](networking/WIFI_RECOVERY_DIAGNOSIS.md)
  and [disabled DNS-monitor lessons](archive/wifi/README.md#disabled-dns-monitor)
- [Native Bus example and known limitations](../examples/bus_app/README.md)

## Documentation conventions

Keep current instructions with their topic. Put completed milestone reports
and superseded proposals in the appropriate archive folder, preserving unique
measurements and evidence. Archive checkboxes and commands describe their
capture date, not a current work queue. Remove redundant notes when their
useful findings are retained elsewhere, and update incoming links after moves.
