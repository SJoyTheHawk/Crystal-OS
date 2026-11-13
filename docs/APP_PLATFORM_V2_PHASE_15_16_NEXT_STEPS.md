# Phase 15/16: step-by-step core development check

The focused device check passed on 2026-11-14. This short procedure is retained
for reproducing the core flows after future changes. Previous A–R results still
count; no screenshots, memory captures or detailed report are needed.

## 1. Put the updated build on the board

Connect the board by USB. In Terminal:

```sh
cd "/Users/szemy/Workspace/ESP32 Crystal OS"
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
ls /dev/cu.usbmodem*
```

Use the port printed by the last command. For example, if it is
`/dev/cu.usbmodem1101`:

```sh
idf.py -p /dev/cu.usbmodem1101 app-flash storage-flash monitor
```

The firmware and panel assets are already built. This updates those two
partitions and preserves installed apps and their registry/state. The existing
test key 240 stays in place. If flashing cannot open the port, close another
serial monitor using it and retry. There is no need to run a full flash.

**Look for:** the device reaches its normal launcher. Leave the monitor open
if convenient; collecting its output is unnecessary unless something fails.

## 2. Open and pair the WiFi panel

Connect the board to WiFi. On the board, open
**Settings → Developer Tools → Local Control Panel** and enable it. Open the
shown `http://<device-IP>/` address in a browser on the same WiFi. Reload once
to load the updated panel, enter the on-screen code, and choose **Pair browser**.

**Look for:** the installed-app list and device controls appear. This is setup,
not a repeat of the pairing/security tests.

## 3. Install the updated Dashboard sample

If **Status Dashboard** is already installed, close it on the device. In the
browser app list, choose its **Uninstall**, confirm, then **Refresh** until it
is gone. The installer requires removing an existing copy before replacement;
this removes only Dashboard and its saved data.

Under **Install app**, select:

```text
/Users/szemy/Workspace/ESP32 Crystal OS/.cache/phase15-16-device-test/dashboard-ui-v1-signed.capp
```

In the macOS file picker, press **Command–Shift–G** and paste that full path if
the hidden `.cache` folder is difficult to find. Choose **Install app**. The
package is already signed with the existing test key; no new keys are needed.

When installation succeeds, use the panel's **Device → Reboot** and confirm.
After boot, enable Local Control Panel and pair again as in step 2. Restart
applies the installed app; re-pairing after restart is expected.

**Look for:** Status Dashboard appears in the launcher. Firmware flashing alone
does not update an installed Dashboard's JSON, which is why this step is needed.

## 4. Check logs and Refresh together — core panel flow

In the browser, open **Device → Live logs → Start logs**. On the board, launch
**Status Dashboard**. While logs are active, press the browser's **Refresh** once.

**Look for:** a new lifecycle message from opening Dashboard; Refresh completes
and the panel stays responsive while log following remains active. A quiet log
window while the device is idle is normal. Leave logs running for the next steps.

## 5. Check text input — core app framework flow

On Dashboard, choose **Controls**. Scroll down inside the Controls content to
**Focus input**, tap it, and type a short note. Tap another control or an empty
area of the app to dismiss the keyboard; the keyboard's **Done/checkmark** also
works.

**Look for:** the existing system keyboard opens, the note appears in the input
field, and a tap elsewhere dismisses the keyboard without blocking that control's
action. While using it, notice whether the field remains reachable and readable;
there is no separate layout inspection.

## 6. Check list, chart and dialog updates — same core flow

Stay in Controls and try each button once:

| Action | What to look for |
| --- | --- |
| **Add list item** | The button is directly below the list. A third row appears immediately; scroll inside the list if needed. |
| **Update chart** | The button is directly below the chart. Its line changes to a clear high-low zigzag. No need to measure pixels. |
| **Show dialog** | An **Example** dialog appears above the app. Tap **OK** or **Cancel**; it closes and the app responds again. |

The small Controls panel scrolls to reach each widget group; each update button
is placed beside the widget it changes. The sample dialog is created once per
app launch and removed when dismissed;
there is no need to open it repeatedly.

## 7. Check one app switch, then stop

Tap **Focus input** again to open the keyboard. Use the normal system navigation
to leave Dashboard, open **Calculator**, then return to Dashboard once.

**Look for:** the keyboard closes when leaving, Calculator works normally,
and Dashboard opens and responds again. Dashboard's temporary sample contents
may reset when its UI is rebuilt; keeping the typed note is not a requirement.
The browser should still be usable.

If steps 4–7 work, the core development check is complete. A short
“Panel and Dashboard controls work” is enough. If something fails, tell me the
step and what happened; include the nearby error only if readily available.
Do not rerun the whole sequence to investigate it yourself.

## Worth a quick look, optional

These are useful only if convenient, and do not hold up the milestone:

- **Sign out once in the browser:** controls and local log output should clear.
  You can end the session there; no need to pair again just for this check.
- **Open Weather normally once:** if internet access is available, notice
  whether it loads or handles a failed request without freezing the OS.
  Report an obvious problem; no forced disconnects, retries or memory recording.

Combined-load profiling, HTTPS root-cause diagnosis, interruption/malformed-input
matrices, soak tests, discovery edge cases and production catalog deployment
belong to later hardening. Only an observed failure that prevents a core flow
needs fixing now. Automated regressions and diagnosis remain the agent's work.
