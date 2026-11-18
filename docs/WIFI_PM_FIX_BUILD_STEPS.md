# WiFi Stability Issue: Investigation Status

**Date:** 2026-11-18
**Status:** ROOT CAUSE NOT YET IDENTIFIED

## Initial Hypothesis (Disproven)

We initially suspected `CONFIG_WIFI_RMT_STA_DISCONNECTED_PM_ENABLE` was causing DNS failures after WiFi reconnection.

**This hypothesis is incorrect:**

1. Waveshare's ESP32-P4 reference firmware has `WIFI_RMT_STA_DISCONNECTED_PM_ENABLE=y` (enabled)
2. Their firmware works reliably - no reports of DNS failures
3. Crystal OS with PM **disabled** still exhibits the same DNS error pattern
4. Therefore, PM is not the root cause

## Actual Problem

Crystal OS exhibits DNS resolution failures after WiFi signal loss:

```
I (35330) eh_wifi: rx RPC StaDisconnected ssid="Vplace7B-3" reason=34
I (40170) esp_netif_handlers: sta ip: 192.168.0.47  // Reconnected successfully
I (41793) esp-tls: couldn't get hostname for :rt.data.gov.hk: getaddrinfo() returns 202
```

Pattern: WiFi reconnects successfully and gets IP, but DNS immediately fails with error 202 (EAI_ADDRFAMILY).

## S3→P4 Migration Issue

Crystal OS was ported from ESP32-S3 (native WiFi) to ESP32-P4 (C6 hosted WiFi via esp-hosted). The issue is likely in:

- Crystal HAL's WiFi event handling with hosted WiFi architecture
- Network interface state management during disconnect/reconnect
- DNS resolver state corruption specific to esp-hosted
- Interaction between Crystal OS's reconnection logic and C6 RPC layer

Waveshare's P4 firmware doesn't exhibit this because it was built for P4 from the start, not migrated from S3.

## Next Steps

1. Compare Crystal HAL's WiFi event handlers with Brookesia's minimal implementation
2. Check if esp-hosted requires different event handling patterns vs native WiFi
3. Investigate DNS resolver state in lwip after esp-hosted reconnection
4. Look for netif lifecycle differences between S3 native and P4 hosted WiFi
