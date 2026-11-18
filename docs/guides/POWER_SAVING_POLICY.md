# Power saving profiles

The normal profile defaults to dimming after 120 seconds. Energy Saving
defaults to 30 seconds and applies 60% of the selected brightness: 100% becomes
60%, and 30% becomes 18%. Whole-percent output rounds to nearest (1% stays 1%;
0% stays off). Disabling Energy Saving restores the saved
brightness preference. Auto Dimming remains the master timeout switch.

The two dim profiles remain editable in Display & Power and are saved
separately. New profile keys replace the legacy shared dim timer so existing
installations receive the new defaults. Screen-off defaults to five minutes.
A saved screen-off time that would occur before or at dimming is extended to
the next supported later timeout; Never and already-later values are retained.
The old implicit halving of both timeouts has been removed.

Energy Saving no longer changes CPU frequency, either at boot or when toggled.
The P4 keeps its configured default CPU speed (360 MHz in this build).
Brightness, timers and the existing Wi-Fi power-save request apply without
restart. The CPU restart prompt and action have been removed.

On-device validation:

1. Enable Energy Saving and confirm brightness is 60% of the selected value and dimming
   occurs after 30 seconds of inactivity, with no restart prompt.
2. Exercise animated screens, networking and app launches; check for blue
   frames and lcd.dsi underrun logs.
3. Disable Energy Saving and confirm restored brightness and dimming after
   two minutes, without restarting.
4. Boot with Energy Saving saved and confirm the boot-screen brightness scaling
   while the CPU retains its default frequency.

Host regression: `python3 tools/test_power_policy.py`. This tests production
policy functions, persistence boundaries, brightness restoration, and removal
of CPU changes and restart prompts; it cannot validate hardware power draw.
