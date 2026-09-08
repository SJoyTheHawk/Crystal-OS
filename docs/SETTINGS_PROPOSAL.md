# Crystal OS Settings Proposal

This document proposes the Settings information architecture for Crystal OS. It
starts with the recommended user-facing structure and Phase 11 scope. The full
candidate list and code/hardware feasibility audit are in the appendix.

The proposal borrows familiar grouping from iOS and Android, and the useful
device-management parts of Raspberry Pi OS, while keeping Crystal OS an
appliance rather than imitating a phone or desktop computer. A setting belongs
here only when a person can understand its effect and the device can honor it
reliably.

Implementation lives in `PHASE_11_SETTINGS.md`, which settles five questions this
document leaves open: that timeouts apply whether or not Energy Saving is on, that
these five categories supersede `DESIGN.md` §8, that Settings is a shell-owned
page rather than an app, that timezone entries carry DST rules, and that dim
brightness is in HAL units. It also supersedes `DESIGN.md` §3 and §8, which are
edited as part of the phase.

## 1. Recommendation

Use five root categories in Phase 11:

1. **Network**
2. **Display & Power**
3. **Sound**
4. **Region & Time**
5. **System**

Add **Apps** in Phase 13 when clear-data and immediate registry updates are
complete. Add **Accessibility** later only after its choices can be applied
consistently across the shell and every bundled app.

Do not keep a category named **General**. Timezone, location, units, device
identity, and reset actions are unrelated tasks and are easier to find under
specific names. Also move the current IP address from System into Network.

### 1.1 Feasibility labels

The tables use these labels:

| Label | Meaning |
| --- | --- |
| **Existing** | The code already has the required backend. Phase 11 mainly needs UI and wiring. |
| **Extend** | Supported by the ESP32-S3 or existing hardware, but Crystal needs a small or moderate HAL/service addition. |
| **Later** | Useful and possible, but depends on a later roadmap phase or broad cross-app work. |
| **Exclude** | Unsupported by this board, misleading with the present hardware, or not worthwhile for Crystal OS. |

"Existing" does not mean that the Settings row already exists. It means the
underlying operation or value is present in the current codebase.

## 2. Suggested Categories and Sub-items

### 2.1 Network

| Sub-item | Phase | Feasibility | Notes |
| --- | --- | --- | --- |
| Wi-Fi | 11 | Existing | On/off toggle with `On`, `Off`, `Connected`, or `Not connected` summary. |
| Wi-Fi Networks | 11 | Existing | Open the Phase 9 scan/connect/forget page; do not build a second list. |
| IP Settings | 11 | Extend | Automatic (DHCP) or Manual. Required by the current Phase 11 plan. |
| Manual IP fields | 11 | Extend | IP address, subnet mask, gateway, primary DNS, and optional secondary DNS. Disabled while DHCP is selected. |
| Connection Details | 11 | Extend | SSID, signal strength, IP address, gateway, DNS, and MAC address. Read-only. |
| Device Name | 11 if time permits | Extend | Friendly hostname, for example `crystal-kitchen`; prepare for future mDNS/remote management. |
| Add Hidden Network | Later | Extend | Manual SSID, security, and password entry. Useful, but not required for the Phase 11 exit gate. |
| Saved Networks | Later | Extend | Current firmware retains one station configuration. A real list needs Crystal-owned credential storage and selection policy. |
| Reset Network Settings | 12 | Later | Forget credentials and restore DHCP after an explicit confirmation. |

Recommended page layout:

```text
Network
  Wi-Fi                         On - Home
  Wi-Fi Networks               Connected

Connection
  IP Settings                  Automatic
  Connection Details           192.168.1.42
  Device Name                  crystal-display
```

Wi-Fi Networks must reuse `wifi_page_open()` and its existing scan, credentials,
connect, and forget flows. IP configuration should be committed atomically after
validating all fields; a half-written static configuration must never be applied.

### 2.2 Display & Power

| Sub-item | Phase | Feasibility | Notes |
| --- | --- | --- | --- |
| Brightness | 11 | Existing | Mirror the quick-panel control. Range remains 0-95 because the HAL deliberately clamps it. |
| Dim After | 11 | Extend | Suggested choices: Never, 15 sec, 30 sec, 1 min, 5 min. |
| Dim Brightness | 11 | Extend | Suggested range: 5-50 in HAL units on the same 0-95 scale as Brightness, never raising a user-selected lower brightness. |
| Turn Screen Off After | 11 | Extend | Suggested choices: Never, 1 min, 2 min, 5 min, 15 min. Must be later than Dim After. |
| Energy Saving | 11 | Existing/Extend | The flag exists. Phase 11 must add the promised CPU, Wi-Fi power-save, brightness-ceiling, and timeout effects. |
| Battery Status | 11 | Existing | Percentage and charging state. Show `No battery` or `Unavailable` when the PMIC cannot report a battery. |
| Enable Energy Saving at Low Battery | Later | Extend | Useful when a battery is fitted; suggested thresholds: Off, 10%, 20%, 30%. |
| Reduce Motion | Later | Extend | Belongs here until a complete Accessibility page exists. Requires one shell-wide animation policy. |

Recommended page layout:

```text
Display & Power
  Brightness                   75%
  Dim After                    30 seconds
  Dim Brightness               20%
  Turn Screen Off After        1 minute

Power
  Energy Saving                Off
  Battery                      82% - Charging
```

Timeouts should work whether or not Energy Saving is enabled. Energy Saving may
shorten the selected timeouts, but it should not be the switch that decides
whether screen lifecycle exists at all. Automatic light sleep remains excluded:
the continuously scanned RGB panel can blank or tear.

### 2.3 Sound

| Sub-item | Phase | Feasibility | Notes |
| --- | --- | --- | --- |
| Volume | 11 | Existing | Mirror the quick-panel 0-100 control and persist it. |
| Timer & Alarm Sounds | 11 | Extend | On/off toggle checked before the timer chime is played. |
| Test Sound | 11 | Existing | Play the existing short timer chime. This is an action row, not a toggle. |
| Touch Sounds | Later | Extend | Possible through the speaker, but repeated codec use must not make touch feel slow. Default off. |
| Separate Alarm Volume | Later | Extend | Useful only after Crystal has more than one sound type. Keep one master volume in v1. |

The board has an audio codec and speaker output, but Crystal currently uses it
only for the timer chime. Do not create media, ringtone, notification, input,
balance, or audio-route controls until corresponding audio services exist.

### 2.4 Region & Time

| Sub-item | Phase | Feasibility | Notes |
| --- | --- | --- | --- |
| Set Time Automatically | 11 | Extend | Controls SNTP. Show the last successful synchronization time when known. |
| Timezone | 11 | Existing/Extend | Required. Present friendly city/UTC choices and store the corresponding POSIX TZ string internally. Every entry must carry its full DST transition rules, not just an offset. |
| Time Format | 11 | Extend | 12-hour or 24-hour; apply to the indicator bar and Clock app together. |
| Set Date & Time | 11 | Existing | Enabled when automatic time is off; write both system time and the PCF85063 RTC. |
| Location | 11 | Existing/Extend | Automatic from network or Manual. Existing cached coordinates already drive Weather. |
| Manual Location | 11 | Existing/Extend | City name, latitude, and longitude with range validation. Coordinates can live under Advanced. |
| Temperature Unit | 11 if time permits | Extend | Celsius or Fahrenheit; convert for display rather than changing stored weather data. |
| Wind Speed Unit | Later | Extend | km/h, mph, or m/s. Useful, but lower priority than timezone and location. |

Recommended page layout:

```text
Region & Time
  Set Time Automatically       On
  Timezone                     Hong Kong (UTC+08:00)
  Time Format                  24-hour
  Set Date & Time              Automatic

Location & Units
  Location                     Automatic - Hong Kong
  Temperature                  Celsius
```

Never expose a raw POSIX timezone string in the normal UI. Use a compiled,
bounded timezone list appropriate to the product's markets. Crystal does not
need the full desktop timezone database in Phase 11.

### 2.5 System

Use subpages so informational, diagnostic, update, and destructive actions are
not mixed together.

| Sub-item | Phase | Feasibility | Notes |
| --- | --- | --- | --- |
| About | 11 | Existing/Extend | Device model, hardware revision, Crystal OS version/build, ESP-IDF version, company information. |
| Legal & Attribution | 11 | Existing | Show Crystal licensing and the required ESP-IDF and `esp-brookesia` credits. |
| Device Status | 11 | Existing/Extend | Uptime, Wi-Fi state, IP, battery state, free internal heap, free PSRAM, and storage use. Keep technical values on this subpage. Read once on open with a manual refresh row; no live timer, because battery shares the touch controller's I2C bus. |
| Restart | 11 | Existing | Call the same restart path used by the five-second hardware-button hold, after confirmation. |
| Software Update | 12 | Later | Current version, check/install update, progress, result, and rollback status. |
| Recovery Information | 12 | Later | Last reset reason and a quiet previous-crash notice. Detailed coredumps stay off the normal UI. |
| Reset Network Settings | 12 | Later | Clears Wi-Fi credentials and manual IP configuration only. Lives under Network, where users look for it; listed here only because it sits with the other reset actions. |
| Reset All Settings | 12 | Later | Restores system settings without deleting installed app data. Requires a defined key inventory. |
| Factory Reset | 12/13 | Later | Clears settings, credentials, registry choices, and app data. Preserve firmware and OTA recovery data. |

Suggested About contents:

```text
About
  Device                       Crystal OS Display
  Hardware                     Waveshare ESP32-S3-Touch-LCD-4B
  Crystal OS                   <project version / build ID>
  ESP-IDF                      <runtime version>
  Serial / Chip ID             <derived device identifier>
  Legal & Attribution          >
```

Restart and all reset actions belong at the bottom. Reset rows need a precise
description of what will be erased and a second confirmation step.

## 3. Recommended Phase 11 Cut

The following is the balanced implementation target. It fulfills the existing
roadmap without pulling Phase 12 reliability or Phase 13 app management forward.

### Must ship

- Settings root, subpage navigation, and two-level Back behavior.
- Network: Wi-Fi entry that reuses the existing page.
- Network: DHCP/manual selection and validated IP, subnet, gateway, and DNS.
- Display & Power: brightness, dim timeout, off timeout, dim level, and Energy
  Saving.
- Power settings apply from stored values instead of compile-time constants.
- Energy Saving implements its promised CPU cap, Wi-Fi modem power save,
  brightness ceiling, and shorter effective timeouts.
- Region & Time: friendly timezone selection that updates the current display
  immediately and survives reboot.
- Region & Time: automatic/manual location and validated manual coordinates for
  Weather.
- System: About and Legal & Attribution.
- System: Restart.

### Should ship

- Network: read-only connection details.
- Display & Power: read-only battery status.
- Sound: volume, Timer & Alarm Sounds, and Test Sound.
- Region & Time: automatic time toggle, manual date/time, and 12/24-hour format.
- System: lightweight Device Status.

### May slip without failing Phase 11

- Device Name.
- Hidden-network entry.
- Temperature and wind units.
- Automatic Energy Saving at low battery.
- Reduce Motion.

## 4. Interaction and Data Rules

- The root page shows a short live summary on each row, such as the SSID,
  timezone, or Energy Saving state. It should not show explanatory paragraphs.
- Controls that already exist in Quick Settings are mirrors of the same stored
  value, not separate settings or duplicate implementations.
- Changes that are safe and reversible apply immediately: brightness, volume,
  timezone, time format, units, and toggles.
- Network configuration applies only after the complete form validates.
- Rows unavailable because of state are disabled with a reason: manual IP fields
  while DHCP is on, manual time while automatic time is on, and manual location
  fields while automatic location is on.
- Settings should remain usable offline. Only scanning, time synchronization,
  automatic location, and update checks require a network.
- Use at least 44x44 px touch targets and 16 px body text on this low-density
  480x480 panel.
- Persist compact values in the existing `crystal` NVS namespace. Define each
  key, type, default, valid range, and migration behavior before implementation.
- Do not poll the shared I2C bus to animate settings. Battery remains a cached,
  low-frequency reading.
- Destructive actions always show their exact scope and require confirmation.

## 5. Codebase and Hardware Findings

This proposal was checked against the codebase as it exists before Phase 11.

### Already available

- `IBrightness` supports get/set, clamps hardware brightness to 95%, and restores
  the persisted `brightness` value during HAL initialization.
- Codec volume supports get/set from 0-100 and restores the persisted `volume`
  value. The timer has a working three-tone speaker test.
- `IWifi` supports radio enable/disable, asynchronous scans, RSSI/security scan
  results, connect, one retained station configuration, reconnect attempts, and
  forget.
- The Wi-Fi page already implements scan, password entry, connect, connected-row
  state, and forget. Settings must reuse it.
- `IPower` reads AXP2101 battery percentage and charging state. The service polls
  it no more often than every 30 seconds.
- The core loads a stored POSIX timezone at boot, initializes from the RTC,
  synchronizes from `pool.ntp.org`, and can write manual local time to both the
  system clock and RTC.
- Weather persists latitude, longitude, and city, and validates coordinate ranges
  when loading them.
- The registry can persist per-app enabled state and slot order, although changes
  currently apply on the next boot.
- NVS encryption, coredump storage, two OTA slots, and a storage partition are
  already configured.
- The hardware button and ESP-IDF already provide a restart path.

### Requires new Crystal interfaces or service work

- The public Wi-Fi HAL has no getters for IP, gateway, netmask, DNS, MAC, channel,
  or live RSSI, and no DHCP/static-IP methods. ESP-IDF can provide them, but the
  access must be added through `IWifi` so the simulator remains usable.
- The Wi-Fi implementation retains only one station configuration. Multiple saved
  networks require a Crystal-owned credential model, secure storage rules, and a
  connection-selection policy.
- Power timeout and dim values are compile-time constants. The current timeout
  check runs only when `power.saving` is enabled.
- The Energy Saving flag exists, but the code does not yet apply all promised
  dynamic-frequency, Wi-Fi modem, brightness-ceiling, and shorter-timeout effects.
- Time synchronization is currently automatic whenever Wi-Fi obtains an IP, uses
  a fixed `pool.ntp.org` server, and has no user-visible enabled state or last-sync
  value.
- The indicator bar converts time to 12-hour format unconditionally. A time-format
  setting must update both the shell and Clock app.
- Weather location is loaded once into service globals. Changing coordinates at
  runtime needs an API to replace the cached location and trigger a refresh.
- The generic storage interface can get, set, or erase one known key. It cannot
  enumerate keys, clear an app namespace, calculate per-app use, or reset a
  defined group of settings.
- Runtime version, reset reason, heap, PSRAM, flash/storage use, and network
  details are available from ESP-IDF, but are not exposed through Crystal-owned
  interfaces. Each one needs a Crystal interface and a simulator stub for the same
  reason static IP does; `sim/crystal_hal_mock.cpp` is already behind the current
  HAL and will not build until it is updated.
- OTA partitions exist, but download, signature policy, progress, rollback, and
  recovery UI are Phase 12 work and are not implemented.

### Hardware limits and exclusions

- There is no ambient-light sensor, so automatic brightness cannot reflect room
  light without adding external hardware.
- There is no GPS receiver. Automatic location can only be network-derived;
  precise location must be entered manually or supplied by external hardware.
- Automatic light sleep is unsafe for v1 because the RGB panel depends on
  continuous DMA scan-out.
- Bluetooth LE exists in the ESP32-S3 silicon, but Bluetooth is disabled in the
  build and Crystal has no Bluetooth service, pairing model, or supported device
  profiles. Do not expose a functional Bluetooth settings page yet.
- The board has an IMU, microphone input, and audio output, but Crystal currently
  has no motion/orientation service, microphone service, recording model, or
  privacy/permission UI.
- Crystal has no telephony, cellular radio, NFC, biometric sensor, secure enclave,
  account system, GPS, or vibration motor. Settings copied from phones for those
  features would be false affordances.
- The current fonts are Montserrat Latin sizes selected at build time. A language
  selector cannot honestly promise CJK or other scripts without localization,
  font, keyboard, storage, and layout work.
- There is no text-to-speech or accessibility semantics layer, so a screen reader
  is not currently practical.

## Appendix A. Full Candidate Settings Catalog

This is the full list considered for Crystal OS. Useful settings remain listed
even when they should be delivered after Phase 11. Excluded rows document why a
familiar phone or desktop setting should not appear.

### A.1 Network and Connectivity

| Candidate | Disposition | Reason |
| --- | --- | --- |
| Wi-Fi on/off | Phase 11 - Existing | Implemented and persisted. |
| Current network | Phase 11 - Existing | SSID and connection state exist. |
| Available networks | Phase 11 - Existing | Existing asynchronous scan page. |
| Connect to secured/open network | Phase 11 - Existing | Existing credentials flow. |
| Forget current network | Phase 11 - Existing | Existing HAL and confirmation flow. |
| Signal strength | Phase 11 - Existing/Extend | RSSI exists in scans; add a live-detail getter. |
| DHCP/manual IP | Phase 11 - Extend | Required roadmap item; ESP-IDF supports it. |
| IP address | Phase 11 - Extend | Move from System into Network details. |
| Subnet mask | Phase 11 - Extend | Needed for manual configuration. |
| Gateway | Phase 11 - Extend | Needed for manual configuration. |
| Primary/secondary DNS | Phase 11 - Extend | Needed for manual configuration. |
| MAC address | Phase 11 - Extend | Useful read-only identity and diagnostics value. |
| Device hostname | Suggested - Extend | Useful for identification and future mDNS. |
| Add hidden network | Later - Extend | Useful on managed networks; needs manual SSID flow. |
| Multiple saved networks | Later - Extend | Current ESP-IDF configuration retains only one network. |
| Auto-connect per network | Later - Extend | Only meaningful after multiple saved networks exist. |
| Network priority | Later - Extend | Only meaningful after multiple saved networks exist. |
| Reset network settings | Phase 12 - Later | Needs defined credential and static-IP reset scope. |
| NTP server | Later - Extend | Technically easy, but most users should keep the default pool. Put under Advanced. |
| HTTP proxy | Later - Extend | Possible, but every network client must honor it consistently. |
| Captive portal assistant | Later - Extend | Requires detection and a constrained authentication UI. |
| Wi-Fi hotspot | Exclude for v1 | Technically possible, but conflicts with the appliance use case and adds security/support burden. |
| VPN | Exclude for v1 | No VPN stack or general network-routing layer. |
| Ethernet | Exclude on target | No onboard Ethernet interface. Reconsider only for an external adapter product. |
| Cellular/mobile data | Exclude | No cellular modem. |
| Airplane mode | Exclude | With only one active radio, the Wi-Fi toggle already expresses this. |
| Bluetooth | Later, not Phase 11 | Hardware-capable but software, pairing, profiles, and validation are absent. |
| NFC | Exclude | No NFC hardware. |

### A.2 Display, Interaction, and Power

| Candidate | Disposition | Reason |
| --- | --- | --- |
| Brightness | Phase 11 - Existing | HAL and quick-panel persistence exist. |
| Dim timeout | Phase 11 - Extend | Replace compile-time constant with validated NVS value. |
| Screen-off timeout | Phase 11 - Extend | Replace compile-time constant with validated NVS value. |
| Dim brightness | Phase 11 - Extend | Replace compile-time constant with validated NVS value. |
| Energy Saving | Phase 11 - Extend | Flag exists; promised system effects remain. |
| Battery percentage | Phase 11 - Existing | PMIC reports it. |
| Charging state | Phase 11 - Existing | PMIC reports it. |
| Low-battery automatic saving | Later - Extend | Useful after battery behavior is validated with a real cell. |
| Reduce motion | Later - Extend | Useful, but needs a central animation policy. |
| Theme: dark/light | Later - Extend | Possible, but every app and status-bar contrast path must support it. Current product is deliberately dark. |
| Accent color | Exclude for v1 | Cosmetic surface area without appliance value. |
| Automatic brightness | Exclude on target | No ambient-light sensor. |
| Color temperature/night light | Exclude for v1 | No established panel color-control path; low value for this display. |
| Screen resolution/refresh rate | Exclude | Fixed integrated panel and timings. |
| Manual screen rotation | Exclude for v1 | Square panel reduces value; shell and touch transforms are fixed. |
| Automatic rotation | Later only if needed | IMU exists, but a rotation service and whole-UI support do not. Low value on a square panel. |
| Screen saver | Exclude for v1 | Card content is already the useful idle display; dim/off solves power use. |
| Always-on display | Exclude | The LCD is already continuously scanned and has no low-power display mode. |
| Light sleep/deep sleep toggle | Exclude from normal settings | Unsafe for the current RGB display path and wake behavior. Keep engineering experiments out of user settings. |
| Power-off action | Exclude until validated | Restart exists; a safe PMIC shutdown/wake contract is not implemented. |

### A.3 Sound and Audio

| Candidate | Disposition | Reason |
| --- | --- | --- |
| Master volume | Phase 11 - Existing | Codec adapter and persisted quick control exist. |
| Timer/alarm sounds toggle | Phase 11 - Extend | Simple policy around the existing chime. |
| Test sound | Phase 11 - Existing | Existing chime can be invoked. |
| Touch sounds | Later - Extend | Feasible, but must not add latency or excessive I2C/audio work. |
| Separate alarm volume | Later - Extend | Useful when multiple sound classes exist. |
| Alarm tone selection | Later - Extend | Requires additional bundled tones and preview UI. |
| Microphone enable/mute | Later | Hardware exists, but there is no microphone service or consumer. |
| Input gain | Later/Developer | Only useful after a recording or voice feature exists. |
| Audio output routing | Exclude for v1 | Only the supported speaker path is integrated. |
| Balance/equalizer/media volume | Exclude for v1 | No media playback service. |
| Vibration/haptics | Exclude | No vibration motor or haptic actuator. |

### A.4 Region, Time, Location, and Units

| Candidate | Disposition | Reason |
| --- | --- | --- |
| Automatic time | Phase 11 - Extend | SNTP already exists; add persisted policy. |
| Timezone | Phase 11 - Existing/Extend | Boot storage exists; add friendly selection and live apply. |
| 12/24-hour format | Phase 11 - Extend | Useful internationally; shell and Clock must share it. |
| Manual date/time | Phase 11 - Existing | Core can update system clock and RTC. |
| Last time synchronization | Suggested - Extend | Useful status value; store the successful sync timestamp. |
| Custom NTP server | Later - Extend | Advanced diagnostic/deployment option. |
| Automatic network location | Phase 11 - Existing | Current Weather service already resolves and caches it. |
| Manual city/coordinates | Phase 11 - Extend | Storage exists; runtime update API and form are needed. |
| Celsius/Fahrenheit | Suggested - Extend | Straightforward display conversion. |
| Wind units | Later - Extend | Straightforward display conversion. |
| Distance/measurement system | Later | Add only when more than Weather consumes it. |
| Language | Later | Requires localization infrastructure and additional fonts. |
| Keyboard layout | Later | Requires localized keyboard definitions and input policy. |
| Wi-Fi regulatory country | Developer/provisioning only | Important technically, but should be tied to product provisioning or region, not casually changed. |
| GPS location | Exclude | No GPS hardware. |

### A.5 Apps and Home Experience

| Candidate | Disposition | Reason |
| --- | --- | --- |
| Enabled apps | Phase 13 - Existing/Extend | Registry persists state, currently applied next boot. |
| Card order | Phase 13 - Existing/Extend | Registry persists slots, currently applied next boot. |
| Open launcher | Phase 13 - Extend | Brookesia launcher exists but Crystal does not currently route to it. |
| Per-app version/author | Phase 13 - Extend | Requires catalog metadata. |
| Clear app data | Phase 13 - Later | Storage cannot enumerate or clear an app prefix yet. |
| Per-app storage use | Later | Requires NVS/SPIFFS ownership accounting. |
| App permissions | V2 - Later | The planned package/runtime permission model belongs to Phases 14-17. |
| Background activity controls | V2 - Later | Current lifecycle already allows only one live app. Add only with new runtime capabilities. |
| Default apps | Exclude for v1 | Crystal has no competing handlers for links, calls, media, or files. |
| Notifications per app | Later | No notification center or per-app notification service exists. |

### A.6 System, Storage, Update, and Recovery

| Candidate | Disposition | Reason |
| --- | --- | --- |
| Device model/hardware revision | Phase 11 - Extend | Static board metadata or build configuration. |
| Crystal OS version/build ID | Phase 11 - Extend | Available from project/app metadata once consistently defined. |
| ESP-IDF/Brookesia versions | Phase 11 - Extend | Build/runtime metadata. |
| Company information | Phase 11 - Existing | Static content required by the current design. |
| Legal and attribution | Phase 11 - Existing | Required by the project notice and design. |
| Serial/chip identifier | Suggested - Extend | Useful for fleet support; do not present it as a secret. |
| Uptime | Suggested - Extend | Available from monotonic system time. |
| Free internal heap/PSRAM | Suggested - Extend | ESP-IDF APIs are already used elsewhere for diagnostics. |
| Flash/storage use | Suggested - Extend | Useful, but add a storage-info abstraction. |
| Wi-Fi/battery summary | Phase 11 - Existing | Existing state; link to the owning detail page. |
| Restart | Phase 11 - Existing | `esp_restart()` is already used by the hardware button. |
| Software update | Phase 12 - Later | OTA slots exist; updater and rollback flow do not. |
| Update over USB | Phase 12/V2 - Later | Planned recovery/loader transport. |
| Last reset reason | Phase 12 - Later | Planned reliability signal. |
| Previous crash notice | Phase 12 - Later | Coredump partition exists; processing UI does not. |
| Reset network settings | Phase 12 - Later | Needs scoped erasure. |
| Reset all settings | Phase 12 - Later | Needs a settings-key inventory and reset API. |
| Factory reset | Phase 12/13 - Later | Needs safe erasure boundaries and app-data enumeration. |
| Backup/restore | Later | Possible through a PC tool, but there is no portable settings schema yet. |
| User accounts/cloud account | Exclude for v1 | No identity or cloud service. |
| Multi-user support | Exclude | Not useful for the appliance model and unsupported by the architecture. |

### A.7 Accessibility and Input

| Candidate | Disposition | Reason |
| --- | --- | --- |
| Reduce motion | Later - Extend | Feasible after animation behavior is centralized. |
| High contrast | Later - Extend | Requires full theme and per-app validation. |
| Larger text | Later | Current fixed layouts and compiled font sizes need systematic responsive work. |
| Bold text | Later | Requires theme/font policy across all apps. |
| Longer touch-and-hold duration | Later - Extend | Feasible by centralizing gesture thresholds. |
| Touch feedback sound | Later - Extend | Speaker exists; default off. |
| Color-blind palette alternatives | Later | Relevant only after auditing semantic use of color. |
| Screen reader | Exclude for current architecture | No accessibility tree, focus navigation, or TTS service. |
| Voice control | Exclude for v1 | Microphones exist, but speech recognition and privacy infrastructure do not. |
| Switch/keyboard navigation | Later | Possible through external hardware, but no input abstraction or UI focus contract exists. |

### A.8 Privacy and Security

| Candidate | Disposition | Reason |
| --- | --- | --- |
| Show stored-network security type | Suggested - Extend | Useful read-only connection information. |
| Clear saved credentials | Phase 12 - Later | Part of Reset Network Settings. |
| Screen lock/PIN | Later | Technically possible, but needs secure storage, recovery, lockout, and threat-model decisions. |
| Microphone privacy control | Later | Add with the first microphone-using feature. |
| Camera privacy control | Exclude until camera support exists | No Crystal camera service or app currently exists. |
| Location permission per app | V2 - Later | Requires V2 app identity and permissions. |
| Network permission per app | V2 - Later | Already part of the planned package permission model. |
| Diagnostics sharing consent | Later | Add only when Crystal can export or upload diagnostics. |
| Advertising/tracking controls | Exclude | Crystal has no advertising or tracking subsystem. |
| Biometrics | Exclude | No biometric hardware or secure authentication stack. |

### A.9 Developer and Hardware Diagnostics

These should be hidden behind a deliberate developer-mode action, not placed in
the normal root list.

| Candidate | Disposition | Reason |
| --- | --- | --- |
| FPS/performance overlay | Developer - Existing | LVGL performance monitor is enabled. |
| Free heap/PSRAM and low-water marks | Developer - Extend | Useful for app and lifecycle debugging. |
| Wi-Fi RSSI/channel/auth mode | Developer - Extend | Available from ESP-IDF after HAL exposure. |
| Battery raw/status registers | Developer - Extend | Useful for board validation; poll sparingly on shared I2C. |
| Touch coordinate tester | Developer - Existing/Extend | Raw touch interface exists. |
| Reset reason/coredump presence | Phase 12/Developer | Planned reliability diagnostics. |
| App lifecycle timing | Developer - Existing/Extend | Slow create/resume paths are already logged. |
| Export logs over USB | Later | Requires a defined USB transport and framing. |
| IMU calibration/test | Later/Developer | Hardware exists; no Crystal IMU HAL yet. |
| Microphone/speaker test | Later/Developer | Speaker test exists; microphone path does not. |
| GPIO/I2C controls | Exclude from normal UI | Unsafe and board-specific; use dedicated engineering firmware or tools. |
| CPU frequency override | Exclude from normal UI | Energy Saving owns policy; arbitrary values complicate validation. |

## Appendix B. Reference Influences

The grouping was informed by these platform conventions, not copied wholesale:

- iOS: specific top-level pages, live row summaries, and destructive reset
  actions grouped under a deeper system page.
- Android: Network & Internet owns both Wi-Fi selection and connection details;
  Quick Settings mirrors the same underlying controls.
- Raspberry Pi OS: hostname, localization, network configuration, interfaces,
  system information, and update/recovery controls are valuable on a managed
  device, while desktop display and boot options are not appropriate here.

Useful upstream references:

- Apple, *Find settings on iPhone*: <https://support.apple.com/guide/iphone/iph079e1fe9d/ios>
- Apple, *Reset iPhone settings to their defaults*: <https://support.apple.com/guide/iphone/iphea1c2fe48/ios>
- Android Help, *Connect to Wi-Fi networks*: <https://support.google.com/android/answer/9075847>
- Raspberry Pi documentation, *Getting started / Configuration*: <https://www.raspberrypi.com/documentation/computers/getting-started.html#configuration>

## Appendix C. Repository Evidence

The primary local sources used for the audit are:

- `components/crystal_hal/include/crystal_hal.hpp`
- `components/crystal_hal/src/crystal_hal.cpp`
- `components/crystal_core/include/crystal_core.hpp`
- `components/crystal_core/src/crystal_core.cpp`
- `components/crystal_shell/src/crystal_shell.cpp`
- `components/crystal_registry/src/crystal_registry.cpp`
- `components/crystal_app/src/crystal_app.cpp`
- `components/weather_app/src/weather_app.cpp`
- `main/main.cpp`
- `sdkconfig.defaults`
- `partitions.csv`
- `docs/DESIGN.md`
- `docs/IMPLEMENTATION_PLAN.md`
- `docs/CODE_GUIDE.md`
- `docs/APP_PLATFORM.md`
