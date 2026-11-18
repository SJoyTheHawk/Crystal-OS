# Historical Wi-Fi investigations

Archived on November 19, 2026. Start with the [current Wi-Fi checks](../../networking/WIFI_CHECKS.md)
and [incident diagnosis](../../networking/WIFI_RECOVERY_DIAGNOSIS.md) for active validation.
The records here preserve observations and abandoned approaches; their code
examples and recommendations are not current recovery policy.

## Disproved power-management hypothesis

Disabling disconnected power management did not resolve the reported DNS
failure. The old idle investigation and stability-fix notes are retained only
for their attempted configuration changes and reconnect-race review:

- [Idle investigation](WIFI_IDLE_ISSUE_INVESTIGATION.md)
- [Stability investigation](WIFI_STABILITY_FIX.md)

The redundant `WIFI_PM_FIX_BUILD_STEPS.md` was removed. Despite its filename,
it contained no build procedure. Its useful correction is retained here:
Wi-Fi could regain an IP address and still return DNS error 202 after signal
loss; the root cause was unresolved, and turning off disconnected power
management was not a verified fix.

## Unverified socket-leak explanation

[Socket-leak investigation](WIFI_DNS_SOCKET_LEAK_BUG.md) preserves the original
timeout/DNS log sequence. A leak, resolver corruption, and upstream fault
were hypotheses without socket-lifetime evidence or a confirmed upstream
issue. Do not treat those explanations as established causes.

## Disabled DNS monitor

[Disabled monitor record](WIFI_DNS_RECOVERY_IMPLEMENTATION.md) preserves the
false-positive failure: the probe hostname failed while real API requests
succeeded, leading to unnecessary recovery/reboots. The monitor was disabled.

The superseded `WIFI_DNS_RECOVERY_ENHANCEMENT_PLAN.md` was removed. Its proactive
probe and automatic-recovery proposal is superseded by the disabled-monitor
record. Passive monitoring and recovery alternatives in that record are
proposals, not implemented policy.

See the [archive index](../README.md) and [documentation index](../../README.md).
