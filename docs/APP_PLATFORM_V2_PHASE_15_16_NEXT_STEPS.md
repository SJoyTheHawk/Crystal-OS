# Phase 15/16 — next steps toward closure

Prepared 2026-11-13. Use with the [closure checklist](APP_PLATFORM_V2_PHASE_15_16_CLOSURE_CHECKLIST.md).
A–M results already recorded remain valid. The user has now reported N–R;
see the [recorded evidence](test-evidence/phase15-16/2026-11-13-batch-nr-results.txt).
N confirms captions, O reconnect UI, P browser logs/sign-out, Q management
cleanup (the user's second P), and R real-token alteration/invalidation on
GET /api/v1/system. Do not repeat those passed observations. Both phases
remain open for implementation/contract work and the final focused batch.

## 1. N–R checks — results received, instructions retained

The following instructions are retained for reproducibility, not a new request
to run N–R again. Exact success-notice wording and broader token/expiry
coverage remain itemized in the closure checklist. Continue at Part 2.
Do not start interrupted-transfer, resource-limit or simultaneous-load
experiments manually; those need controlled fixtures or fixes first.
O's panel+TLS capture succeeded but reached 5807 free internal bytes and a
3328-byte largest internal block at different samples, before Dashboard
started. Investigate headroom before the controlled combined-load test.

### N. Prepare the board and confirm the caption fix

1. From the project root, activate the environment and set these variables:

   ```sh
   source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
   export CRYSTAL_URL="http://192.168.1.193"
   export CRYSTAL_TEST="$PWD/.cache/phase15-16-device-test"
   umask 077
   ls -lh "$CRYSTAL_TEST/dashboard-signed.capp" \
     "$CRYSTAL_TEST/action-guards-signed.capp"
   idf.py build
   ```

   The signed fixtures and trusted test key ID 240 already exist. Do not
   generate replacement keys or provision the key again. If a fixture is
   missing, stop this setup and report the missing filename.
2. Close the existing serial monitor, then flash only the application:

   ```sh
   idf.py -p /dev/cu.usbmodem1101 app-flash monitor
   ```

   This deploys the built caption fix. Do not run a full flash. Confirm
   signing key ID 240, zero packages, four built-ins and the device IP.
   Update CRYSTAL_URL if the IP changed. Record version/ELF prefix.
3. Enable Local Control Panel and pair the browser with the new code. Install
   dashboard-signed.capp and action-guards-signed.capp, one at a time. There
   should be six entries in the management catalog. Restart once to apply
   both, then verify the six launcher entries, re-enable the panel and pair
   again. The serial monitor can remain open for these Wi-Fi uploads.
4. Look at Status Dashboard and Action Guard Check in the launcher. Long
   captions must stay on one centered line within their own cells, using
   ellipsis when necessary. They must not overlap or move adjacent icons.
   Both management lists should retain the full application names.

**Send N:** caption photo, full names in management lists, six-app count and
boot version/ELF/key ID. This tests the source fix already made, not a new
layout redesign.

### O. Confirm Dashboard's final reconnect UI state

1. Open Dashboard's Status tab and observe Network: connected with no offline
   hint. Keep the time visible.
2. In device Settings, turn Wi-Fi off. Return to Dashboard: disconnected and
   the offline hint should appear, while time continues.
3. Turn Wi-Fi on, wait for its connection/IP, then return to Dashboard. Within
   a few seconds of returning, require Network: connected and the hint gone.
4. If the IP changed, update CRYSTAL_URL. Enable/pair the panel again as needed.

**Send O:** explicit confirmation of the final connected/hint-hidden screen
or a photo, plus the serial reconnect lines. This closes the missing K UI
observation; network reconnect logs alone do not close it.

### P. Validate the existing browser Live logs viewer

1. In the paired web panel open Device, then Live logs, and press Start logs.
2. Use the device touchscreen to open Dashboard, then Calculator, then
   Dashboard again. Require newly arriving lifecycle lines in the browser.
3. Press Pause logs. Switch apps on the device once more; no new text should
   be appended while paused. Press Start logs and repeat a switch to confirm
   new delivery resumes. A resumed stream can replay retained history.
4. Pause logs before other API operations. The current server stream is
   synchronous and ends after about 60 seconds; this check does not close
   concurrent API responsiveness or server shutdown during active streaming.
5. Repeat the basic Start/Pause and one device app switch in a mobile browser.
   Turn the panel off/on and use a fresh PIN when moving pairing to the other
   browser; old sessions are invalidated by this step.
6. Sign out and confirm the viewer and its output are cleared/hidden with
   device controls. Do not report missing serial diagnostics as missing app
   logs; this viewer does not contain every ESP-IDF message.

**Send P:** desktop/mobile results, example fresh lifecycle lines, Pause/Start
behavior, sign-out clearing, any timeout or frozen interaction. Report an
unexpected failure and continue only independent checks; do not keep retrying.

### Q. Finish the missing uninstall observations

1. Obtain a new PIN and pair the browser. In Apps, choose Status Dashboard's
   Uninstall action, then Cancel. Refresh: Dashboard remains installed and
   can still launch; the catalog still contains six entries.
2. Uninstall Action Guard Check in the browser and wait for completion.
   Refresh: five catalog apps remain.
3. On the device open Settings → Manage Apps and uninstall Dashboard there.
   Record the exact success notice before leaving the page.
4. Return to the shell. Confirm four app/card indicator positions and four
   launcher apps with no gaps. Refresh the paired browser: four built-ins,
   no Dashboard/guard app.
5. In the browser inspector's Network tab select the authenticated
   GET /api/v1/apps response. Confirm HTTP 200 and four built-ins with
   Dashboard absent. Copy only the response JSON, not Authorization headers.

**Send Q:** Cancel outcome, exact on-device uninstall notice, four indicator/
launcher positions, and the apps response. Do not reinstall after this step.

### R. Check altered and invalidated real tokens

This last check uses a new CLI pairing and leaves the four-app baseline intact.
Turn the panel off/on and reserve its new PIN for the CLI; do not enter it in
the browser. In the second terminal, with the variables from N still set:

```sh
python3 - <<'PY'
import getpass
import json
import os
from pathlib import Path
import sys
sys.path.insert(0, 'tools')
import crystal
folder = Path(os.environ['CRYSTAL_TEST'])
token = crystal.pair_device(os.environ['CRYSTAL_URL'], getpass.getpass('NEW device PIN: '))
path = folder / 'closure-session.json'
path.write_text(json.dumps({'token': token}), encoding='utf-8')
path.chmod(0o600)
print('Paired; token saved locally and not printed.')
PY
CRYSTAL_TOKEN="$(python3 -c 'import json,os; from pathlib import Path; print(json.loads((Path(os.environ["CRYSTAL_TEST"])/"closure-session.json").read_text())["token"])')"
CRYSTAL_BAD_TOKEN="$(python3 -c 'import json,os; from pathlib import Path; t=json.loads((Path(os.environ["CRYSTAL_TEST"])/"closure-session.json").read_text())["token"]; print(("0" if t[0] != "0" else "1") + t[1:])')"
curl --silent --show-error --max-time 10 \
  -H "Authorization: Bearer $CRYSTAL_TOKEN" \
  --write-out '\nHTTP %{http_code}\n' "$CRYSTAL_URL/api/v1/system"
curl --silent --show-error --max-time 10 \
  -H "Authorization: Bearer $CRYSTAL_BAD_TOKEN" \
  --write-out '\nHTTP %{http_code}\n' "$CRYSTAL_URL/api/v1/system"
```

Expect the actual token to return HTTP 200, and its one-character alteration
to return HTTP 401 with unauthorized JSON. Do not share the token/file contents.

Turn the panel **off and back on** on the device, then run this without pairing
again first, using the same old token still in the terminal:

```sh
curl --silent --show-error --max-time 10 \
  -H "Authorization: Bearer $CRYSTAL_TOKEN" \
  --write-out '\nHTTP %{http_code}\n' "$CRYSTAL_URL/api/v1/system"
unset CRYSTAL_TOKEN CRYSTAL_BAD_TOKEN
```

Expect HTTP 401. This proves session invalidation for this endpoint, not
every protected route or actual 24-hour token expiry. The remaining route/
expiry coverage belongs in the controlled host/final validation batch.

**Send R:** 200/401/401 results with response bodies, no token values. Finish
with four enabled built-ins in original order and the panel off.

## 2. Finish implementation before requesting more hardware tests

These are coding tasks for the implementation agent, not menus or commands
the user can run to enable an unfinished feature.

1. Reconcile catalog/browser-transport/API wording against the agreed scope.
   Prepare the exact facade function matrix against plan sections 6.1/6.2.
   Ask about a user-visible product decision only if documents/session do
   not resolve it; do not ask the user to infer technical implementation gaps.
2. Implement browser USB installation and page-delivery workflow. Test with
   a supported browser and explicitly account for unsupported browser cases.
3. Fix server log servicing, token/session checks during streams and clean
   shutdown. Define whether CLI follow reconnects, then validate the existing
   browser log client against that contract.
4. Complete shared handle/property/event operations, dynamic lists/charts,
   textarea focus/keyboard, visible dialog actions and lifecycle teardown.
5. Classify Dashboard complexity/startup budget and address timing accurately.
6. Prepare isolated rejection/recovery fixtures and a reliable method to
   request HTTPS while Dashboard+panel remain active. Do not instruct the
   user to wait indefinitely for an untriggered automatic request or alter
   the Dashboard's allowed actions just for this test.
7. Run targeted host regressions and the integrated build. Provide the tested
   artifact/configuration and one combined device test batch. Changes to web
   assets require their normal system-assets deployment as well as app flash;
   N's caption-only app flash does not deploy later web changes.

## 3. Close the hosted catalog workflow

The project needs the actual HTTPS hosting location and ownership of its
catalog/publication signing identity. Existing package test key ID 240 is
not automatically a production catalog identity. Prepare files locally before
publication, then verify the hosted signed index, package size/hash/signature,
download/install and listing update without device firmware OTA.

No server build service is required. Do not silently deploy to an account or
publish a release while merely preparing the local test workflow.

## 4. Run one final board batch after the fixes

This next implementation-dependent batch covers browser USB, concurrent logs
and API responsiveness, stream/session shutdown, controlled malformed/
interrupted USB recovery, safe admission rejection, new facade/keyboard/dialog
operations, combined Dashboard/panel/HTTPS memory measurements and final
cleanup. It incorporates outstanding N–R failures instead of repeating passes.

For the inherited cold-boot HTTPS failures, capture failure and retry/recovery
through completion and classify the cause; do not require every external
network request to succeed or call a timeout an allocation error.

## 5. Freeze and close

Review/freeze ABI v1 only after the facade matrix and tests are complete.
Reconcile the implementation/design/code/test records and attach evidence.
Mark Phase 15 and Phase 16 closed only when required features and their
validation are complete, with any scope change explicitly recorded. Commit
or release publication is a separate requested action.

## N–R reply format

```text
N — caption photo/full management names, app count, boot version/ELF/key ID
O — final connected state and offline hint hidden after reconnect
P — desktop/mobile fresh logs, Pause/Start, sign-out clearing
Q — browser Cancel result, device success notice, four indicators/apps, apps JSON
R — valid token HTTP 200; altered token HTTP 401; invalidated old token HTTP 401
Unexpected failures — step and serial/browser excerpt, with tokens omitted
```
