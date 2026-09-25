# Phase 3 Core Services Validation

Phase 3 now includes the UI event queue, toast overlay, RTC-first system clock,
SNTP correction, and display inactivity handling.

## Hardware test

Build, flash, and monitor the board:

```bash
idf.py build
idf.py -p /dev/cu.usbmodem101 flash monitor -b 2000000
```

Verify the following behavior:

1. `Core services ready` appears shortly after the launcher starts.
2. If PCF85063 contains valid time, the log reports `System time initialized
   from PCF85063` and the status-bar clock shows that time.
   If the RTC has never been set or reports a stopped oscillator, the status bar
   shows `--:--` instead of Brookesia's default `00:00 AM`.
3. Wi-Fi reconnects when ESP-IDF has a saved station configuration. After an
   SNTP correction, `Time synchronized` appears and the corrected local time is
   written to PCF85063.
4. Leave the screen untouched for 30 seconds. Backlight brightness ramps down
   to 20 percent.
5. Continue without touching until 60 seconds. The backlight ramps off.
6. Touch the screen. Brightness ramps back to 95 percent.

The timezone defaults to Hong Kong time (UTC+08:00), represented by the POSIX
value `HKT-8`. The sign is reversed by POSIX convention. A future Settings
implementation will store the selected POSIX timezone value under the HAL
storage key `timezone`, overriding this first-boot default.

The first touch that wakes a dimmed or dark screen may still reach the active
app. Swallowing that wake touch belongs to the Phase 6 gesture arbiter, which
will own input before app dispatch.

SNTP and the future manual date/time setting both update system time and write
the same local time to PCF85063. Manual settings use `crystal_time_set()`.
