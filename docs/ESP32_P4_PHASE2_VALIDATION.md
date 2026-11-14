# ESP32-P4 Phase P4.2 validation

Date: 2026-11-14 (Asia/Hong_Kong). Starting checkpoint: `bc55dc2`.
Status: implemented, P4 build/flash/startup smoke checks pass; physical visual
acceptance and sustained animation performance qualification remain open.

## Scope and design

This implements **Phase P4.2: Visual Enhancements** in
[the v2 migration plan](CRYSTAL_OS_V2_P4_MIGRATION_PLAN.md#phase-p42-visual-enhancements).
The older plan's Phase 2 Display & Bootloader overlaps the working Phase 1
foundation; no board driver or bootloader replacement is necessary for this UI work.

- **Theme:** the accepted Brookesia 0.4.2 / LVGL 8.4 square dark stylesheet moves
  from `main.cpp` into `crystal_stylesheet.cpp`. The 720px dimensions, accepted
  typography, one-app lifecycle and disabled duplicate Brookesia pill remain.
  Shared visual tokens use Crystal's existing dark surfaces, blue accent,
  teal success and red failure states. No unfinished light-theme API is exposed.
- **Boot:** a P4 top-layer overlay renders after display/HAL initialization and
  before signature/package initialization. It shows Crystal OS, an 84px spinner,
  real initialization milestones and a progress bar. Text is 48px/20px. A bounded
  FreeRTOS mailbox copies each update, so callers never mutate LVGL from another
  task or leave pointers to temporary text. NVS/display operations before the
  first drawable frame are not presented as already visible progress.
- **Startup completion:** Ready holds for 300ms and fades for 240ms. A ready
  callback deletes the overlay after animation; cancellation is not used as a
  completion callback. The overlay and shell startup gate retain input ownership
  during the fade. Wi-Fi association is not a boot prerequisite. Fatal failures
  after display setup show an explicit failure state before the existing abort/
  recovery path; package-storage failure retains the existing degraded behavior.
- **Navigation:** P4 Settings pages now enter with an in-place 300ms opacity fade;
  exit retains its existing 36px motion plus fade, using ease-in-out. The parent
  remains visible and opaque until its child fully covers it, then is hidden.
  Outgoing page input is disabled
  recursively; deletion happens only on completion. At most one retiring page is
  retained, and another navigation settles it first. Crystal's existing card
  lifecycle/snapshots and gesture arbitration remain authoritative. P4 card
  settle/fade is 300ms, Quick Settings settling is 350ms, and the centered white
  home pill is 216x6px with 180ms release feedback. S3 keeps its prior motion and
  pill dimensions and has no new boot overlay or Settings transition.
- **Wi-Fi:** retain the existing Settings overlay and HAL. Refresh shows an inline
  blue spinner and disables repeat requests; the hosted scan RPC runs in a worker
  so LVGL stays responsive. The scan state ends on completion, worker failure,
  timeout or page teardown. Rows show three signal bars, a truncated SSID,
  secured/open/joining state and a connected check. Duplicate SSIDs keep their
  strongest result, connected networks sort first, and callbacks select from the
  rendered snapshot. Open networks connect without a password prompt; secured
  networks retain Crystal's Show/Hide dialog and four-plane keyboard. Long SSIDs
  are ellipsized; P4 keyboard key labels use 20px. Accepted Settings text is unchanged.

The factory reference was inspected for staged loading, bounded status updates,
scan feedback and password presentation. Its LVGL 9 APIs were adapted to Crystal's
LVGL 8 APIs; its separate workspace and the existing C6 firmware are unchanged.

## Verified checks

| Check | Result / scope |
| --- | --- |
| P4 build | ESP-IDF v6.1; signed app `0x451000` (4,526,080 bytes), 46% of 8MiB OTA slot free |
| App-only flash | `build/crystal_os.bin` at `0x30000`; esptool hash verified, hard reset |
| Startup capture | 38 seconds; no panic, assertion or watchdog reset observed |
| Boot overlay milestone | 663ms after `app_main`; serial first-frame milestone, not measured panel latency |
| Shell constructed | 1,122ms after `app_main`; 5,128 bytes main-task stack headroom |
| Preserved network services | Association/IPv4, SNTP/RTC sync, Weather fetch and HTTP-200 HTTPS smoke pass using the existing C6 |
| New host visual regression | Real LVGL + AddressSanitizer: P4 and S3 theme, copied cross-task messages, Ready/failure, fade ownership, repeated/interrupted Settings animation cleanup pass |
| New host Wi-Fi view regression | Real LVGL + AddressSanitizer: scan progress, worker failure, timeout, off/empty states, signal rows, non-overlapping name/details, SSID deduplication, stable selection and connect/forget actions pass |
| Existing regressions | HAL scan/reconnect, shell remote launch/clear and real-LVGL launcher/swipe tests pass |

Flashed app SHA-256:
`3fb42097870cc46ba5b661758c49b81547a225e4bbc811cf63093795fab3d4e4`.
Later rebuilds embed new build metadata and can produce another digest.

Host commands:

```bash
python3 tools/test_p4_visuals.py
python3 tools/test_wifi_page.py
python3 tools/test_crystal_wifi.py
python3 tools/test_shell_control_launch.py
python3 tools/test_launcher_swipe.py
```

Local logs: `/private/tmp/crystal-phase2-p4-build.log`,
`/private/tmp/crystal-phase2-p4-flash.log`,
`/private/tmp/crystal-phase2-p4-boot.txt`,
`/private/tmp/crystal-phase2-visuals-test.log` and
`/private/tmp/crystal-phase2-wifi-page-test.log`.
Temporary logs are not durable release artifacts. Serial logs can contain private
network details and are not committed verbatim.

## Performance and limitations

- This P4 uses **360MHz**, and PPA remains disabled. No SDK, board dependency,
  display flush/alignment or hardware acceleration change is part of Phase P4.2.
- The active LVGL redraw period is **30ms**. A 60fps claim is therefore not
  established. The new chained `crystal_ui_perf` monitor reports redraw count,
  window duration, average/max draw time and over-16ms samples. It does not
  measure panel VSYNC or infer animation FPS from an idle display.
- In the startup window, 40 redraws/5,094ms averaged 21ms with a 210ms maximum and
  seven over-16ms samples. Subsequent Weather/idle windows averaged 8ms with 15ms
  maxima. These are mixed startup/idle samples, not sustained navigation benchmarks.
- A fresh full S3 firmware build/flash has not been performed for this revision.
  The host suite compiles and exercises the production S3 fallback paths.
- Existing boot diagnostics remain: unsupported panel swap-XY, early unset RTC,
  C6 version mismatch despite successful networking, no initial package signing
  key, and a GPIO conflict diagnostic. A separate pending brightness migration
  uses the overlength NVS key `brightness.scale`, producing
  `ESP_ERR_NVS_KEY_TOO_LONG`; this is outside the Phase P4.2 edits and should be
  corrected before that brightness work is closed. Do not update the working C6
  in response to version discovery messages alone.

## Device acceptance still required

Current review checkpoint: 2026-11-15. The user accepted the boot logo's size and
placement; stationary Settings entry and parent-to-child coverage; smooth Wi-Fi
scan feedback without the teal blink; password-dialog spacing and button colors;
and the corrected internal network-row padding. The user subsequently confirmed
the four functional groups below pass; Settings animation performance remains
open, with an observed drop to approximately 7fps during the fade.

- [x] Keyboard: outside-tap and Done dismiss it, the dialog re-centers without a
      blank band, tapping the field reopens it, Show/Hide retains typing focus,
      and numbers/punctuation work. Cancel closes the dialog and keyboard.
- [x] Wi-Fi lifecycle: leaving during a scan and reopening remains safe; secured
      connection, failed-connection feedback, connected/forget states and long
      SSIDs remain usable on the revised screen. Actual radio scan duration is
      not yet benchmarked. Open-network testing depends on an available network.
- [x] Settings navigation: rapid Back, another page opened mid-animation, and
      return to the previous app show no stuck input, clipped controls or stale
      pages. Normal entry/coverage is already accepted.
- [x] Boot completion and shell: boot fades cleanly to the restored card; the thin
      pill, swipe-up, card switches and Quick Settings remain coherent without
      duplicate indicators. Logo size/placement is already accepted.
- [ ] Animation performance: investigate the previously reported uneven Settings
      fade and measure repeated physical navigation separately. The current 30ms
      redraw period does not establish the plan's 60fps target; qualification
      remains open until frame timings support it or the user explicitly defers
      that target with the limitation documented.

After these checks, reconcile the migration-plan status and final regression
evidence before closing the phase. A fresh full S3 build/flash remains a separately
disclosed validation gap; host fallback tests are not equivalent to that build.

Do not mark Phase P4.2 complete solely on a successful build or startup capture.

## First device review and focused tuning

The user requested reviewing and tuning one element at a time; the combined
acceptance question is **not** an acceptance of Phase P4.2.

- **Boot artwork pending:** replace the circular spinner with the user's supplied
  Crystal OS logo once available. The boot background is **#11151B**, RGB
  **(17, 21, 27)**. A transparent logo can blend with that surface. The current
  spinner remains a temporary placeholder; no substitute logo was invented.
- **Settings opening:** the user observed the launcher between pausing the app
  and the Settings fade. The transition now captures the current app before
  HOME and retains that image beneath the Settings root, so the existing app
  pause/restore lifecycle does not expose the launcher. The snapshot is cropped
  by the app-area container and released with the root's completed exit or
  interrupted teardown. If image allocation fails, an opaque Crystal surface
  keeps the launcher covered; failure to create that surface leaves the app up.
- The real-LVGL/AddressSanitizer regression checks the captured app pixels,
  retention through enter/exit, cleanup and display teardown. The older Developer
  Tools fixture now loads the production S3 layout constants; it and Manage Apps
  regressions pass. Physical confirmation of the Settings correction is pending.

The first startup metrics and digest above identify the initial Phase P4.2
image. The Settings correction is a subsequent app image; do not attribute the
first capture's measurements to that rebuild.

Settings correction: IDF v6.1 build passes; app-only flash at 0x30000 passes
esptool hash verification. SHA-256:
30a3dade768179597efe8fce0f90291a70f70b694ec522beb5dd53db77f1e74f.
Local logs use the /private/tmp/crystal-phase2-settings- prefix. The boot logo
and further visual changes remain pending the user's element-by-element review.

### Second Settings review: stationary app and parent-to-child fade

The user reported that the app moved upward when opening Settings and that
opening a child category exposed the app behind Settings. Child-to-parent
transitions were reported as working correctly.

- **App position:** LVGL snapshots start at the source object's coordinates.
  Brookesia app screens begin below the status bar; the original backdrop
  placement incorrectly treated the snapshot as starting at display (0,0).
  The image now uses its source screen origin, including any extra draw margin,
  preserving the exact displayed position with no zoom or movement.
- **Entry:** Settings and categories fade in place with no translation. Each
  parent remains visible and fully opaque beneath the incoming child. Only the
  child's completed fade hides the parent; interrupted navigation first makes
  the entering page opaque before hiding its parent. Failure to create a child
  leaves the parent visible. Existing child-to-parent exit motion is retained.
- **Regression:** the real-LVGL/AddressSanitizer P4 and S3 suite checks the 40px
  source-screen offset, unchanged backdrop position throughout entry, zero entry
  translation, parent visibility during child fade, hide-after-completion,
  interrupted entry and cleanup. IDF v6.1 P4 build passes. Physical review remains
  pending; the boot logo is still awaiting the supplied artwork.

The next correction's local logs use the
/private/tmp/crystal-phase2-settings-stationary- prefix. Earlier captures and
digests remain evidence for their respective earlier images.

The stationary-entry correction was app-only flashed at 0x30000 with esptool
hash verification. App SHA-256:
41a2f5cf96e5acd475be40573fbaa7075ec4c5650ca8c72e72306e8b68307364.

### Boot logo supplied and integrated — 2026-11-14

- The user supplied reference/crystalos-boot_icon.png (598x598 RGBA). Its
  original dimensions, bytes and transparency are preserved; original SHA-256:
  49a83f0853a456a928073e01158aac1c4fd48571b807d6e11cf5685d0da46a27.
- A separate 256x256 copy preserves the square proportions and alpha channel.
  It replaces the circular spinner above Crystal OS and remains visible during
  Ready/failure and the existing boot fade. The background remains #11151B.
- The P4-only embedded RGB565/alpha asset is 196,608 bytes and draws at native
  zoom with no runtime PNG decoder or scaling. Generation instructions are in
  components/crystal_shell/assets/README.md. S3 has no boot overlay or asset.
- The production LVGL host render was visually reviewed. P4/S3 visual regression
  checks pass, including actual logo dimensions, transparent corners, no spinner,
  spacing/no clipping, Ready visibility, fade cleanup and existing Settings tests.
- IDF v6.1 P4 build passes: signed app 0x481000 bytes (4,722,688 bytes), leaving
  44% of the 8MiB app slot free. Local logs use the
  /private/tmp/crystal-phase2-boot-logo- prefix.
- The user confirmed both Settings entry transitions now look correct, but
  reported uneven fade smoothness. That performance tuning remains open; this
  logo update makes no change to refresh rate or Settings transition logic.

The earlier pending-artwork notes describe the previous review checkpoints.
The user confirmed on the P4 that the 256x256 boot logo's size and placement
look good (2026-11-14). This accepts the boot artwork and placement only;
fade smoothness and the remaining Phase P4.2 checks stay open.

The logo app-only flash at 0x30000 passes esptool hash verification. Signed app
SHA-256: a0ab21fa46e75620c65a6ee8d921cc9ca90009b224d8e5a6b1d89007864438d4.

### Wi-Fi scan and password review corrections — 2026-11-14

The user reported a whole-page teal frame immediately before the SSIDs appeared,
an approximately three-second pause in the spinner at scan completion, excessive
password/Show spacing, unreadable action buttons, and unused space after keyboard
dismissal. Signal bars and the user-facing secured/open information are retained.

Confirmed source-level causes and changes:

- List sorting called connected() twice per comparison and again per row. On
  P4 each call synchronously queried the hosted C6. The list now takes one
  connection snapshot. DeviceWifi::connected() reads an atomic association flag
  updated by STA_CONNECTED/STA_DISCONNECTED and IP events, so ordinary UI status
  reads no longer make a C6 RPC. This retains association-before-DHCP semantics.
  Explicit connect, forget and radio-off clear the flag. Scan completion also
  avoids its redundant connected-AP query. Radio scan parameters and C6 firmware
  are unchanged; elapsed radio scan duration has not been benchmarked here.
- The P4 list was fixed at 416px high instead of using the available page.
  It now grows below the toolbar and disables elastic/momentum overscroll. The
  completed result update invalidates the full Wi-Fi page after layout, keeping
  replacement rows, spinner removal and previous scroll position in one redraw.
  The user confirmed on the flashed P4 that the spinner stays smooth and the
  whole-page teal blink is gone (2026-11-14). This validates the combined
  correction; the precise flash mechanism is not isolated by the host tests.
  No global display/transition setting is changed by this correction.
- The P4 password field expands from 380px to 432px, leaving 12px to Show/Hide.
  Password input uses a dark Crystal surface; Connect uses accent blue and
  Cancel/Show use neutral slate, with explicit pressed colors and readable text.
  The shared feedback helper removes theme styles, so buttons must supply their
  own background color. Show/Hide cancels deferred outside-tap dismissal while
  typing. Existing keyboard close notifications re-center the dialog.

Verification for this revision:

- Real LVGL/AddressSanitizer exercises the production dialog and keyboard:
  password toggle, colors, spacing, keyboard clearance, outside-tap dismissal,
  dialog re-centering, reopening the keyboard, and full-height list bounds pass.
  Existing scan/timeout/failure/sort/stable-selection tests pass; connection
  queries remain constant per render. Optional host renders are available with
  tools/test_wifi_page.py --output-dir PATH. These do not validate panel frames.
- HAL AddressSanitizer/UndefinedBehaviorSanitizer tests confirm repeated local
  status reads perform no driver queries, pre-DHCP association, disconnect and
  radio-off status, plus the existing scan/reconnect/cancellation cases.
- P4/S3 boot and Settings visual regressions and shell launch/clear tests pass.
- IDF v6.1 P4 build passes; signed app remains 0x481000 bytes, 44% slot free.
  App-only flash at 0x30000 passes esptool hash verification. C6 firmware and
  stored data are preserved. App SHA-256:
  e0adf1a755a3e7e9c883d03435af8b28b1cdb82d4149952cf6d0daff3428be8f.
- A 25-second startup capture contains no panic, assertion or watchdog reset;
  the existing network-connected event and time synchronization are observed.
  The pre-existing brightness.scale NVS key-length warning remains separate.
  Logs use /private/tmp/crystal-phase2-wifi-fix-*; network details are not
  committed. Physical scan feedback is accepted: opening WiFi Networks and
  refreshing retain a smooth spinner, with no whole-page teal blink. Total radio
  scan duration is not benchmarked. The user accepted the password field/Show-Hide
  spacing and Connect/Cancel button colors and readability on the flashed P4
  (2026-11-15). Keyboard dismissal, layout restoration and reopening remain
  pending user review. Phase P4.2 stays open.

### Wi-Fi row side margins — 2026-11-15

At the user's request, network rows now have horizontal space inside the list:
16px on each side on P4, matching Crystal's Settings content inset. Percentage
row widths use the padded list content area, leaving the scrollbar outside the
cards. Signal guides, typography, scan feedback and password layout are retained.
The real-LVGL regression checks both side margins and separation between network
names and details, alongside the existing scan and keyboard cases; it passes.
The IDF v6.1 P4 build and app-only flash at 0x30000 pass, including esptool
hash verification and hard reset. Firmware SHA-256:
44710fb87bbfc02759093323909c61e9eb42f43726b22e9e796d3af8ca1efa14.
Logs use /private/tmp/crystal-phase2-wifi-margins-*. C6 firmware and stored
data are preserved. The user rejected this outer row inset: the intended spacing
is between the row edges and their contents. This revision is superseded below.

### Wi-Fi row internal padding correction — 2026-11-15

Restore full-width row backgrounds by removing the list's horizontal padding.
Increase P4 row content padding from 10px to 24px on each side, so signal bars,
SSID names and status/chevrons sit farther from the row edges. Row height and
vertical padding remain the same; S3 keeps its original 10px content padding.
The production LVGL regression passes and checks full-width backgrounds, left
signal padding, right status padding, and non-overlapping names/details, alongside
the existing Wi-Fi/password/keyboard cases. IDF v6.1 P4 build passes. The user
accepted the corrected internal spacing on the flashed P4 (2026-11-15). App-only flash at
0x30000 passes esptool hash verification and hard reset. Firmware SHA-256:
c4f122814b3050b9c932bd665dc9afffd45fe37a7211b78cabe0e455ea02d6a8.
Logs use /private/tmp/crystal-phase2-wifi-row-padding-*. C6 firmware and stored
data are preserved.

### Keyboard physical sizing and animation discussion — 2026-11-15

The user confirms the four functional acceptance groups pass. They request a
larger keyboard matching the S3's physical key size and report approximately
7fps during Settings fading. Transition implementation changes are explicitly
on hold until the method is discussed with the user.

Both reference boards document a 4-inch square panel: S3 480x480 and P4 720x720.
The shared keyboard had retained a 200px reserved band and 174px actual height
on P4, reducing its physical key height. The P4 band is now 300px, actual keyboard
height 261px, bottom safe inset 39px, outer/row gaps 6px, and column gap 3px.
Key labels increase from 20px to 24px, matching S3's 16px at the 1.5x resolution
ratio. S3 geometry and 16px labels remain unchanged. Password-dialog placement
already follows the reserved keyboard top, so it clears the enlarged band.

The production LVGL/AddressSanitizer host tests compare both targets: first-key
touch areas are 45x38px on S3 and 68x57px on P4 (within one pixel after scaling).
All four keyboard planes, character entry, Done, reopening and viewport height
restoration pass, along with password dialog clearance and existing Wi-Fi tests.
Host renders were reviewed. IDF v6.1 P4 build passes; app size is 0x491000 bytes,
with 43% of the 8MiB slot free. The enlarged keyboard needs physical acceptance.
App-only flash at 0x30000 passes esptool hash verification and hard reset;
firmware SHA-256:
cd5ff68aa2b2def9077c9a2725620858c96baf86c2aee7420458293d428c5943.
Logs use /private/tmp/crystal-phase2-keyboard-size-*. The C6 firmware and stored
data are preserved. No new runtime scan or connection acceptance is inferred
from this flash verification.

The current Settings page is created once; each opacity animation frame renders
its widget subtree into LVGL software layers and blends it with the background.
This is a supported explanation for the observed slowdown, not a completed
timing profile. Merely changing easing to linear or translating the live tree
does not inherently cache it or make it GPU-composited. PPA remains disabled.
Options for discussion are a cached RGB565 page fade (same stationary visual,
still requires pixel blending), an opaque cached slide (less blending, different
motion), or limiting animation to a smaller part of the page. A 720x720 RGB565
snapshot uses 1,036,800 bytes, plus any other retained snapshots and buffers.
No transition method, fade duration, refresh period or display configuration is
changed in this keyboard revision. The reported 7fps is user-observed, not a
transition-specific instrumented measurement. Phase P4.2 remains open.
