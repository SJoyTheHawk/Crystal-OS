> Archived historical procedure. Phase 15/16 are complete; pending items below are historical. See the [current regression guide](../../APP_PLATFORM_V2_PHASE_15_16_TEST_GUIDE.md) and [closure record](../../APP_PLATFORM_V2_PHASE_15_16_CLOSURE_CHECKLIST.md).

# Device test batch — 2026-11-13

Status: A–F closed on 2026-11-13, with host verification of E's transient
startup state/guard. [Closure record and qualification](test-evidence/2026-11-13-batch-af-closure.txt).
This closes this batch only; the phase-exit gates remain separate.

The instructions below are retained for reproducibility; they are not
themselves pass evidence. Icons, basic sign-out, selected-file cleanup and
reboot cancellation are already confirmed and do not need repeating.
Follow-up: A–C have now been reported and recorded in the
[batch evidence](test-evidence/2026-11-13-batch-abc-clock-disable.txt).
D initially exposed a built-in ID parser bug when disabling Clock. The
correction is now device-confirmed by D's follow-up. D–F results are retained
in [the follow-up evidence](test-evidence/2026-11-13-batch-def-persistence-recovery.txt):
Clock enable/order persistence and restoration, controlled reboots, switch
state/service stop/Wi-Fi recovery, and unused-PIN expiry/fresh-code recovery
pass. Original order is restored: Dev Tester, Clock, Weather, Calculator,
all enabled. No additional flash or repetition of those completed checks
is needed. The later user-supplied pairing screen confirms browser reboot
guidance. Starting/locked-switch state is host-verified: fast startup and
the completion-time pairing dialog prevent reliable manual observation.
Weather initially times out but later recovers.

## A. Setup and baseline

Keep the existing USB monitor running throughout browser tests. If it is
closed, start it without requesting a startup reset:

```sh
idf.py -p /dev/cu.usbmodem1101 monitor --no-reset
```

In a separate terminal, set the device's current address:

```sh
CRYSTAL_URL="http://192.168.1.193"
```

Use the IP shown on the device if different. Enable Local Control Panel,
pair one browser, and Refresh. Record firmware 0.15.0, the four built-ins,
their current order and enabled states. The expected package count is zero.
Retain screenshots and serial output, excluding pairing codes/bearer tokens.
Avoid reloading or switching paired browsers unnecessarily: tokens are kept
in browser memory, and a fresh panel enable/pair cycle replaces the session.

## B. Protected endpoint rejection

Run this in the second terminal while the local panel is enabled:

```sh
check_auth() {
  for auth_case in missing invalid; do
    printf '%s %s %s: ' "$auth_case" "$1" "$2"
    if [ "$auth_case" = invalid ]; then
      curl --silent --show-error --max-time 5 --request "$1" \
        --header "Authorization: Bearer invalid-test-token" \
        --write-out ' HTTP %{http_code}\n' "$CRYSTAL_URL$2"
    else
      curl --silent --show-error --max-time 5 --request "$1" \
        --write-out ' HTTP %{http_code}\n' "$CRYSTAL_URL$2"
    fi
  done
}

check_auth GET /api/v1/system
check_auth GET /api/v1/apps
check_auth GET /api/v1/logs
check_auth POST /api/v1/apps
check_auth PATCH /api/v1/apps/order
check_auth POST /api/v1/apps/org.crystal.auth-probe/launch
check_auth POST /api/v1/apps/org.crystal.auth-probe/enabled
check_auth DELETE /api/v1/apps/org.crystal.auth-probe/data
check_auth DELETE /api/v1/apps/org.crystal.auth-probe
check_auth POST /api/v1/system/reboot
```

Expect 20 HTTP 401 responses with unauthorized JSON. These intentionally
omit bodies: authorization must reject before body validation. The probe
app ID is absent, so no existing app is targeted. If any response is not
401 or times out, stop and retain it before continuing. Wait ten seconds
afterward and confirm no reboot/request log, app change, or launch. Refresh
the paired browser: its valid session and four-app list must still work.
This covers missing and made-up tokens only, not actual token expiry.

## C. Unsigned Wi-Fi package rejection

Use /tmp/crystal-uninstall-retest.capp. If the temporary file is gone,
recreate it from the current example:

```sh
./tools/crystal pack examples/declarative_dashboard --unsigned \
  -o /tmp/crystal-uninstall-retest.capp
./tools/crystal inspect /tmp/crystal-uninstall-retest.capp
```

Inspect must identify Status Dashboard and unsigned Developer Mode policy.
In the paired laptop browser, select that file and press Install once.
Wi-Fi is required to reject unsigned packages even if Developer Mode is on.
Expect a visible rejection and HTTP 400 in the browser Network tab; current
firmware reports Unsigned package requires explicit Developer Mode. Refresh
must still show only four built-ins, with no Dashboard. Check Manage Apps
also has four, and the launcher remains gap-free. An installed result or
Dashboard appearing is a failure. This does not validate signed Wi-Fi
installation or prove the configured trusted key.

## D. Settings persistence and one reboot per click

1. In the paired browser, move Calculator to the top using its up arrows.
   Refresh and save the new order. If available, retain /api/v1/apps from
   browser Network: ranks should be consecutive 0 through 3.
2. Disable Clock. Refresh: it remains in the four-entry catalog as Disabled.
   On-device Manage Apps must agree on the saved order/disabled state.
   Launcher changes require reboot; do not judge the live launcher yet.
3. With the same serial monitor still running, press Reboot, read the
   restart/re-pairing explanation, and confirm Reboot now exactly once.
   Capture the request log and following boot in one continuous trace.
4. Expect one reboot-request message, one restart, and browser return to
   pairing with reboot guidance rather than a failed-refresh error. Do not
   restart the monitor or press the physical reset button during capture.
5. After boot, expect firmware 0.15.0, Wi-Fi reconnect, weather/HTTPS success,
   and Local Control Panel off. The launcher should now have THREE enabled
   apps, Calculator first, with Clock absent and no gaps.
6. Enable the panel, pair with its new code and Refresh. The web catalog
   and Manage Apps should still list FOUR apps: Clock is Disabled. Confirm
   order persists and system/apps requests return HTTP 200 in Network.
7. Restore the original recorded order and enable Clock again. Refresh,
   then repeat one confirmed reboot with continuous logging. Re-enable and
   pair the panel after boot. Expect four enabled launcher apps without
   gaps, and device/web order matching the restored order.

The temporary three-app launcher is intentional while Clock is disabled.
Do not count the two deliberate test reboots as duplicate scheduling.

## E. Developer switch and Wi-Fi failure recovery

1. Turn Local Control Panel off, then on with Wi-Fi connected. Observe the
   Starting state, locked switch during startup, then on state and PIN.
   Do not repeatedly tap during startup; record whether it is disabled.
2. Dismiss the dialog, leave and reopen Developer Tools: switch stays on.
3. Pair, then turn the switch off. Browser Refresh should fail to reach the
   device API. The static page may remain visible; that is not proof the
   service is running. This command should fail to connect/time out:

   ```sh
   curl --max-time 5 -i "$CRYSTAL_URL/api/v1/system"
   ```

4. Turn Wi-Fi off/disconnect on the device without forgetting its saved
   network. Try enabling Local Control Panel: it should stay/return off and
   explain that Wi-Fi is needed. No restart or crash.
5. Reconnect Wi-Fi, note the current IP, enable the panel and pair. Refresh
   should work with four enabled apps and the restored order.

## F. Unused pairing PIN expiry

1. Turn the panel off/on to generate a fresh PIN. Note it privately and start
   an 11-minute phone timer. Do NOT use the PIN yet. Keep the panel enabled,
   Wi-Fi connected, and the device running for the entire interval.
2. You may open built-in apps and check normal touch/navigation while
   waiting. Do not reboot, toggle the panel, or change Wi-Fi during the wait.
3. After 11 minutes, enter that same unused PIN in an unpaired browser. It
   must be rejected. Record the error and timing, excluding the PIN itself.
4. Turn the panel off/on, pair with the new code immediately, and Refresh.
   Expect successful recovery. This proves unused-PIN expiry, distinct from
   the already-passed one-time PIN reuse rejection.

## Next batch

Continue with [G–M: signing, Wi-Fi installation, logging and Dashboard checks](APP_PLATFORM_V2_DEVICE_TEST_BATCH_G_M_2026-11-13.md).
G–M results are now recorded: signing/install/logging/run and cleanup pass;
K retains reconnect-UI and simultaneous Dashboard/panel/TLS observations.
See the next batch's review section for qualifications. A–F do not need repeating.

## Report

Send A through F as Pass/Fail/Not run, with the endpoint output, browser
messages/screenshots, and continuous reboot/boot/recovery serial excerpt.
Call out the three-app versus four-app launcher counts and restoration.
Actual signed install, trust-key provisioning, >1 MiB upload progress,
package runtime lifecycle, and live app-log checks remain later work.
