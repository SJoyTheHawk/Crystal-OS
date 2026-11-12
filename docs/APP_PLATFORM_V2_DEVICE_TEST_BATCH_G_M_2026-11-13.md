# Device test batch G–M — 2026-11-13

Status: **G/H/I/J/L/M functional checks reported passed on 2026-11-13; K
partially complete**. [Results and qualifications](test-evidence/phase15-16/2026-11-13-batch-gm-results.txt).
O in the [N–R follow-up](test-evidence/phase15-16/2026-11-13-batch-nr-results.txt)
closes K's reconnect UI observation; combined Dashboard/panel/TLS evidence
still remains. N confirms the launcher caption fix on the device. The
onCreate timing classification remains separate. A–F are already
[closed with their recorded qualifications](APP_PLATFORM_V2_DEVICE_TEST_BATCH_2026-11-13.md).
This batch covers trusted signing, Wi-Fi installs, upload progress, CLI logs/run,
and the implemented declarative Dashboard. It does not close either phase by itself.

The original instructions below are retained for reproducibility; passed
steps do not need repeating. Input focus/keyboard, dynamic APIs and other
unimplemented phase items remain open even when the sample controls render.

## Review of the supplied results

- G/H/I/J/L/M: functional checks passed with the source qualifications in
  the evidence record (no invented HTTP statuses or stored-state assertions).
- K: layout, time/timer bindings, controls rendering and repeated lifecycle
  passed. The supplied pictures show the corrected full app area and separate
  timer buttons. Disconnected status/hint is confirmed.
- K reconnect follow-up: O confirms the instructed online/offline/reconnected
  Dashboard UI sequence. This observation is passed; no repeat is needed.
- K remaining: capture a Weather/HTTPS request while **Dashboard is running and
  Local Control Panel is enabled together**, including the before/TLS-peak/
  released heap lines. J shows Dashboard+TLS; M shows panel+TLS before Dashboard
  starts. Neither excerpt alone establishes all three simultaneously.
- Text-field keyboard/focus is unimplemented; no-keyboard behavior is recorded
  as a limitation, not a full input interaction pass. The 95–120 ms onCreate
  times still exceed the 80 ms budget.
- The long launcher-name report has a source fix: centered, single-line
  ellipsis within the existing cell, preserving the full app name. Host
  real-LVGL checks and `idf.py build` pass; N now confirms the truncation/full
  management-name distinction on the updated device (ELF 07e71ed2e).

Final M cleanup is confirmed by the supplied zero-package boot and built-in
slots 0–3. There is no need to repeat G–M. The two K observations can be added
at the next Dashboard installation; do not count a missing request as a pass.

---

Allow roughly 45–60 minutes, including builds and repeated pairing. Work in order.
If a prerequisite fails, stop the dependent steps and send its result; do not
repeatedly retry an installation or work around a failed signature check.

Use the current device IP; `192.168.1.193` is the last reported address. The
serial monitor can remain open during Wi-Fi installs. Close it before flashing.
These instructions use **app-only flash**; do not full-flash the apps partition.
Keep private keys and bearer tokens out of screenshots and shared transcripts.

## G. Prepare fixtures and provision the test public key

Start in the repository root, with four enabled built-ins and no Dashboard
package. The order is Dev Tester, Clock, Weather, Calculator.

```sh
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
export CRYSTAL_URL="http://192.168.1.193"
export CRYSTAL_TEST="$PWD/.cache/phase15-16-device-test"
umask 077
python3 tools/prepare_phase15_16_device_test.py
cat "$CRYSTAL_TEST/test-public-key.hex"
```

The fixtures have already been generated and host-verified in this workspace.
Rerunning preparation keeps the existing private keys. It makes a clean source
copy because Finder metadata such as `.DS_Store` is rejected by the packer.
If using a different terminal, activate ESP-IDF again: its Python has the
required `cryptography` package. A missing dependency is a setup failure,
not a device result.

| File inside `$CRYSTAL_TEST` | Purpose | Current size |
| --- | --- | ---: |
| `dashboard-signed.capp` | Valid Dashboard, test key ID 240 | 17,275 bytes |
| `dashboard-untrusted.capp` | Different private key, key ID 241 | 17,275 bytes |
| `dashboard-tampered.capp` | Changed signature; file CRCs remain valid | 17,275 bytes |
| `dashboard-large-signed.capp` | Valid Dashboard plus unused upload padding | 1,328,026 bytes |
| `action-guards-signed.capp` | Timer permission and unsupported-action checks | 1,184 bytes |

Sizes may change after future example edits; `fixtures.json` is authoritative.
The large fixture exceeds both 1 MB and 1 MiB. Its padding is an unused package
file, not an image allocated when the Dashboard opens.

IDs 240 and 241 are test identities for this batch. Do not replace an existing
release identity assigned to either ID; report that conflict before provisioning.
The previously configured ID 1 does not need to be changed or removed from NVS.
The test key is local test material, not the project's release signing decision.

1. Run `idf.py menuconfig`. Under **Crystal OS**, set:
   - **Initial trusted app signing key ID**: `240`.
   - **Initial trusted app signing public key (64 hex characters)**: the
     complete output of `test-public-key.hex`, with no spaces or newline.
   Save and exit. Enter only the public hex value, never a private PEM key.
2. Build, then close any serial monitor before the app-only flash:

   ```sh
   idf.py build
   idf.py -p /dev/cu.usbmodem1101 app-flash monitor
   ```

3. Save the boot version and ELF prefix, plus this required message:
   `crystal_signature: Loaded configured package signing key ID 240`.
   A successful build by itself does not prove device provisioning.
4. Confirm LittleFS is ready and there are still four working built-ins.
   Check the assigned IP and update `CRYSTAL_URL` if it changed.

**Report G:** public key ID, public hex value, provisioning boot line, firmware
version/ELF prefix, IP, and baseline count. Never send either private key.

## H. Reject untrusted and tampered signatures

1. Enable Local Control Panel on the device. Pair the normal browser with its
   new code. Keep Developer Mode off. Use the file picker to open the fixture
   folder; on macOS, Command–Shift–G accepts its absolute path.
2. Upload **`dashboard-untrusted.capp`**. Expect refusal, normally HTTP 400
   with `Package signature not trusted`. Refresh: still four catalog apps,
   four launcher apps, no Dashboard and no gaps.
3. Upload **`dashboard-tampered.capp`**. Expect the same refusal and unchanged
   counts. A CRC/header error does not prove this signature test.
4. Refresh normally once more. The paired session should still work.

The other key is not provisioned by these instructions. If the device has an
older unrelated key at ID 241, its public key still will not match this freshly
generated signer. Record the device message rather than assuming its NVS is empty.

**Report H:** each filename, HTTP status, error message, unchanged app counts,
and any serial errors. Both signed files use the Dashboard ID; run these
rejections before installing the valid Dashboard to avoid duplicate-ID masking.

## I. Signed CLI install and live lifecycle logs

Turning the panel off and on invalidates the old browser session and creates a
new one-use code. For this step, give that code to the CLI, not the browser.
Do not pair the browser with it first.

```sh
printf 'Enter the NEW device pairing code: '
read -r CRYSTAL_PIN
./tools/crystal install "$CRYSTAL_TEST/dashboard-signed.capp" \
  --wifi "$CRYSTAL_URL" --pin "$CRYSTAL_PIN" \
  > "$CRYSTAL_TEST/session.json"
echo "Install exit code: $?"
python3 -c 'import json,sys; r=json.load(open(sys.argv[1])); assert r.get("installed") is True and r.get("restart_required") is True and r.get("token"); print(json.dumps({k:v for k,v in r.items() if k != "token"}, indent=2))' \
  "$CRYSTAL_TEST/session.json"
CRYSTAL_TOKEN="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["token"])' "$CRYSTAL_TEST/session.json")"
unset CRYSTAL_PIN
```

Expect exit code 0 and `installed: true`, `restart_required: true`. The CLI's
bearer token stays in the local protected folder; send only the redacted output.
If install fails, do not continue with an empty session file.

Launch through the authenticated API before rebooting:

```sh
curl --silent --show-error --max-time 15 --request POST \
  -H "Authorization: Bearer $CRYSTAL_TOKEN" \
  --write-out '\nHTTP %{http_code}\n' \
  "$CRYSTAL_URL/api/v1/apps/org.crystal.declarative-dashboard/launch"
```

Expect HTTP 202 and `queued: true`; wait for the serial launch result. If
Settings is still open, close it on the device to see the Dashboard behind it.
The queued response alone does not prove the app opened.

Then start the separate app log stream:

```sh
./tools/crystal logs -f --wifi "$CRYSTAL_URL" --token "$CRYSTAL_TOKEN" \
  | tee "$CRYSTAL_TEST/cli-lifecycle.txt"
```

During the next 30–45 seconds, use the **device touchscreen** to switch from
Dashboard to Dev Tester and back twice. Expect corresponding fresh lifecycle
lines in both the CLI stream and serial monitor. Old replayed lines by themselves
do not prove live delivery. The SSE stream includes app lifecycle events; it
is not a copy of every ESP-IDF serial diagnostic.

**Current implementation limit:** this handler serves one synchronous stream
for about 60 seconds and the CLI does not reconnect automatically. Its normal
end is not a reset. Let it finish before browser/API actions. Do not mark
continuous logging, concurrent API responsiveness, or panel/network shutdown
during an active stream as passed from this test; those remain separate work.

**Report I:** redacted install JSON, launch response plus serial outcome, new
live lifecycle lines, stream ending behavior, and any reboot/crash/warning.

## J. Large browser upload and reboot persistence

1. After the log stream ends, turn the panel off/on and pair the browser using
   the new code. The previous CLI token is now invalid.
2. In Apps, uninstall Status Dashboard. Wait for completion and Refresh.
   Confirm four built-ins and no launcher gap. Do not upload a replacement
   while the same app ID is still installed.
3. In Install, choose **`dashboard-large-signed.capp`**. Start a screen recording
   if possible, then Install. Expect upload progress before the final result,
   followed by HTTP 202 and `installed: true`, `restart_required: true`, or the
   browser's equivalent successful-install/restart message.
4. Record whether intermediate percentages are actually visible. A successful
   upload or a final 100% alone does not establish visible progress. If it is
   too fast to judge, report **upload passed; intermediate progress not observed**.
5. Refresh: catalog contains five apps including Status Dashboard 1.0.0.
   Reboot using the confirmed web dialog. After boot, the panel starts off.
   Confirm the signing-key line and LittleFS `1 packages`, with no install
   staging/partial-package error.
6. Launch Dashboard from the device launcher, which should now have five
   working apps and no gaps. Re-enable the panel and pair the browser again.
   Apps and Manage Apps should agree on all five IDs, enabled states and order.

**Report J:** upload size, observed progress, HTTP/JSON or browser result,
post-reboot package count, launch result and management-list agreement.

## K. Dashboard layout, bindings and repeated lifecycle

Keep Wi-Fi connected and the Local Control Panel enabled for the initial checks.
Save the serial log through this sequence.

1. **Layout:** the dark Dashboard fills the app area below the system status
   bar. No large white area or tiny upper-left layout. Status, Timer and Controls
   tab titles fit. Take one photo of Status and one of Timer.
2. **Time/network:** leave Status visible for at least 15 seconds. Time advances
   and network shows connected. Raw `{{...}}` text, stale time and clipping fail.
3. **Timer:** Start 5 min; observe it counting down from about 300. Pause, wait
   10 seconds, and confirm the value holds. Resume and confirm it decreases.
   Reset and confirm zero/stopped. Running/Paused indicators match the state.
   The four buttons must be readable and separate, in two rows.
4. **Controls:** scroll from the button matrix to the bottom. Confirm the switch
   toggles visually, slider moves, weather image renders, two list items are
   visible, the static chart has its line, the text field renders, and the spinner
   animates. Button-matrix buttons are wired to timer reset; test one after
   starting the timer, then leave the timer reset.
5. **Pause/resume:** open Settings from Dashboard and return three times. Time
   and network must catch up on return; the timer must not acquire duplicate
   updates. **Destroy/recreate:** launch Dev Tester and then Dashboard five times
   using the device UI. Expect lifecycle transitions, complete redraws, no stale
   tab objects and no watchdog/allocator/LVGL failure. The one-running-app
   eviction warning is expected. Record any `onCreate ... budget 80 ms` warning
   separately; a successful render does not close that performance concern.
6. **Network binding:** after the log stream has ended, turn Wi-Fi off in device
   Settings and return to Dashboard. Network changes to disconnected and its
   offline hint appears while the clock continues. Turn Wi-Fi back on, wait for
   an IP and return: connected state recovers and the hint disappears. Re-enable
   the panel/pair again if needed. No need to repeat A–F's PIN expiry test.
7. Save any available `heap before`, `tls-peak` and `heap released` lines around
   Weather/HTTPS requests and Dashboard activity with the panel enabled. Note
   allocation failures or continuing memory loss. If no request runs during
   this state, report it; earlier boot heap measurements do not prove this
   combined Dashboard/panel/HTTPS case.

The chart/list here are static samples. The text field's keyboard/focus flow
and dynamic list/chart APIs remain open. The example dialog starts hidden and
does not prove visible-dialog interaction. Report what is visible; do not turn
these missing capabilities into manual pass results.

**Report K:** layout photos, timer observations, controls results, lifecycle and
network sequence, serial errors/performance warnings and memory observations.

## L. Action guards and package enable/disable persistence

1. With Dashboard's timer reset, use the paired browser to install
   **`action-guards-signed.capp`**. This is a separate ID:
   `org.crystal.action-guards`, display name **Action Guard Check**.
   There should now be six catalog entries. Launch it from the web panel;
   close Settings if needed to see it.
2. Tap **Try timer without permission**. Expect the serial warning
   `timer action rejected; package lacks system_timer permission`.
   The remaining value stays zero; it must not start a five-minute timer.
3. Tap **Try unsupported action**. Expect
   `rejected unsupported declarative action 'system.reboot'`.
   The app/device must continue normally, with no reboot or side effect.
   This intentionally unsupported command tests the allowlist.
4. Uninstall Action Guard Check, wait for completion and Refresh. Confirm
   five apps remain, including Dashboard, and no launcher gap.
5. Disable **Status Dashboard**, Refresh, then reboot. After boot it stays
   in both management lists as Disabled but is absent from the launcher,
   leaving four working built-ins. Pair again after boot as usual.
6. Enable Dashboard, reboot and pair again. It returns as the fifth enabled
   launcher app. Both management lists agree. Launch it and reset the timer
   if needed. Save the relevant lifecycle/registry lines.

**Report L:** both rejection warnings with no side effects, guard-app removal,
disabled/enabled persistence after each reboot, and launcher/catalog counts.

## M. Full `crystal run`, clear-data and final cleanup

1. Uninstall Status Dashboard in the browser and wait for the four-app baseline.
   Then turn the panel off/on. Reserve its new PIN for the CLI.
2. Run the entire pack/install/launch/log command against the clean source:

   ```sh
   printf 'Enter the NEW device pairing code: '
   read -r CRYSTAL_PIN
   ./tools/crystal run "$CRYSTAL_TEST/dashboard-source" \
     --wifi "$CRYSTAL_URL" --pin "$CRYSTAL_PIN" \
     --key "$CRYSTAL_TEST/test-private.pem" --key-id 240 \
     -o "$CRYSTAL_TEST/dashboard-run.capp"
   ```

   Expect install success/restart requirement, a queued launch, then lifecycle
   output. Confirm the app really opens without needing a reboot. Close device
   Settings to see the selected app behind it. Use device navigation while
   streaming; let the approximately 60-second stream end before HTTP actions.
   A duplicate-ID refusal means step 1 did not finish; it is not a run pass.
3. Turn the panel off/on and pair the browser. Confirm five apps. With Dashboard
   running, choose Clear data and Cancel first: app and counts stay unchanged.
   Then confirm Clear data: the package remains installed, any running instance
   is cleaned up, and Launch creates a fresh instance without a crash.
   The Dashboard does not expose saved note state, so do not claim a persistent
   data-erasure assertion just because its field appears empty. The shared
   system timer is not app-owned persisted data.
4. Uninstall Dashboard. Wait for completion; Apps, Manage Apps and the launcher
   should return to the four enabled built-ins with no gaps. Check the original
   order Dev Tester, Clock, Weather, Calculator, and restore it if necessary.
5. Reboot once more. Confirm LittleFS `0 packages`, four built-ins, no guard
   package/Dashboard and no launcher gap. Local Control Panel starts off.
   Keep the generated local test keys if retesting is needed; do not distribute
   them or delete a release key as part of cleanup.

**Report M:** full run output, actual launch and live events, clear-data outcome,
and the final clean boot/catalog/launcher result.

## Send the results together

```text
G — PASS / FAIL / NOT RUN: key ID, public hex, provisioning line, ELF/IP
H — untrusted: status/message/count; tampered: status/message/count
I — CLI install JSON without token; queued + actual launch; live log lines
J — bytes/progress; browser result; post-reboot package/app counts
K — layout/timer/controls/lifecycle/network; warnings and heap evidence
L — permission/unsupported-action warnings; no side effects; disable/enable persistence
M — run output/live events; clear-data; four-app cleanup after reboot
Unexpected resets, allocation errors or failed steps — serial excerpt
```

These results can close the corresponding implemented-feature checks after
review. Browser Web Serial, on-device catalog browsing/download, unfinished
dynamic/focus APIs, ABI v1 review/freeze, and the live-stream limitations remain
separate implementation/exit items. Do not mark Phase 15 or Phase 16 closed
solely because G–M pass.
