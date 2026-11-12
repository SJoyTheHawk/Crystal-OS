# Phase 15 and Phase 16 closure checklist

Reconciled 2026-11-13 against the implementation plan, current source, and
[A–F](test-evidence/phase15-16/2026-11-13-batch-af-closure.txt) /
[G–M](test-evidence/phase15-16/2026-11-13-batch-gm-results.txt) /
[N–R](test-evidence/phase15-16/2026-11-13-batch-nr-results.txt) hardware evidence.
Neither phase is closed. Passed tests below should not be repeated merely
because older copies of a checklist still have empty boxes.

Follow the [ordered next steps](APP_PLATFORM_V2_PHASE_15_16_NEXT_STEPS.md) for
the implementation-dependent final batch. N–R results are recorded; do not
repeat their passed observations. The user's second P heading maps to Q.

## Already established

Wi-Fi panel serving and desktop/mobile operation, pairing and PIN expiry/reuse
rejection, missing/invalid-token rejection, app management and reboot, configured
test key ID 240, signed CLI/browser installs, untrusted/tampered signature
rejection, visible 1328026-byte upload progress, USB CLI install/reboot discovery,
CLI lifecycle logs and complete crystal run, Dashboard layout/timer/permissions,
repeated lifecycle and cleanup, and final zero-package/four-built-in boot.
N–R additionally establish launcher caption truncation with full management
names, Dashboard reconnect UI, desktop/mobile browser log pause/restart and
sign-out, cancellation/cleanup, and real-token alteration/invalidation on
the system query. Manual Refresh is currently needed to observe some queued
uninstall completions; it shows the correct state in Q.
Clock/Weather/Calculator visual expressibility is host-verified. Their native
computation/fetch behavior is not being moved into declarative Phase 16.

## Phase 15 — implementation and contract closure

- [ ] **P15-1 — Browser USB transport.** Implement and validate installation
  through the same browser panel over physical USB (plan Phase 15 exit
  criteria). The Python USB installer already working does not implement the
  required browser path. Specify how the browser page is delivered and which
  supported browser/security context is used; preserve Wi-Fi support on the
  browsers already tested. Owner: implementation agent, then board validation.
- [x] **P15-2 — Browser log viewer functional device validation.** P reports
  fresh lifecycle messages, Pause/Start behavior, sign-out hiding output and
  controls, and mobile confirmation in addition to the desktop observations.
  Source/host tests establish the bounded client and split-frame handling.
  The device retains at most 64 volatile log lines across panel/Wi-Fi toggles;
  newly paired clients can read that history. Browser sign-out clears its
  view, not the device queue; reboot resets the queue. Stream servicing and
  in-flight authorization remain P15-3, not established by this functional pass.
- [ ] **P15-3 — Log-stream servicing and shutdown.** Current HTTP logs handler
  runs a synchronous approximately 60-second loop; CLI does not reconnect.
  Ensure other panel/API actions remain responsive during streaming, and
  disabling the panel, disconnecting Wi-Fi, signing out or invalidating the
  session ends access appropriately. Define reconnect/cursor behavior if
  continuous follow is promised; test shutdown without UI/server deadlock.
  Owner: implementation agent, then board validation.
- [ ] **P15-4 — Close the HTTPS catalog workflow and reconcile its scope.**
  The resolved decision in plan section 15.2 and the overview's decisions
  table specifies computer-side fetching of a signed online index and
  prebuilt signed packages. CLI signing/list/download tools exist, but no
  hosted catalog URL or completed hosted end-to-end evidence is recorded.
  Select/configure hosting and a catalog trust identity, publish a test index
  and package, and verify catalog fetch/signature, package hash/signature,
  download and installation. A listing update must not require firmware OTA.
  Earlier checklist wording additionally demands on-device catalog browsing;
  align it with the agreed workflow instead of silently treating a new device
  store as mandatory. Owner: implementation agent for workflow/docs; project
  owner for hosting/signing identity. Production release-key ownership is
  separate from the already-provisioned local package test key.
- [ ] **P15-5 — Finish API/discovery contract comparison.** Check plan section
  9.3 against actual handlers, schemas and errors, including builtin DELETE
  versus disable, launch policy, and specified discovery/address behavior
  (section 9.2 names crystal-<serial>.local). Record differences and fix them
  or explicitly agree/document the final contract. Basic IP access already
  passes; absence of an mDNS capture does not by itself prove implementation.
  Owner: implementation agent.

## Phase 15 — remaining device/test evidence

- [ ] **P15-T1 — USB interruption/malformed input.** Exercise truncated or
  malformed transfer and reboot during install; require no partial registered
  app, retained working baseline, and successful subsequent valid install.
  Source-folder USB installation is also still unrecorded as a separate path.
  These extend the passed successful-transfer test. Owner: host harness plus
  controlled board session; preserve installed data.
- [ ] **P15-T2 — Remaining token-lifecycle coverage.** R establishes a valid
  token returning 200, a one-character alteration returning 401, and the
  old token returning 401 after panel restart, all for GET /api/v1/system.
  Do not repeat those three checks. Complete the real-token all-route guard
  comparison and expiry validation strategy using real-handler simulated-clock
  host tests; a 24-hour manual wait is not required. In-flight stream
  invalidation stays in P15-3. The ten-minute PIN expiry is already passed.
  Owner: implementation agent for host coverage and any targeted board delta.
- [ ] **P15-T3 — Remaining management details.** Record on-device Uninstall
  notice wording and complete any missing cross-interface metadata/action
  comparison. Q confirms browser cancellation, Action Guard removal, on-device
  Dashboard uninstall, correct launcher/card positions, and HTTP 200 apps list
  with four enabled built-ins in slots 0–3. Exact on-device success-notice text
  was not supplied. Queued browser uninstall currently needs manual Refresh;
  source tells the user to Refresh and Q confirms eventual consistency.
  Include that behavior in the API/UI contract audit rather than claiming
  automatic list updates. Owner: implementation agent for the remaining audit.
- [ ] **P15-T4 — Device resource-admission rejection.** Capture refusal when
  declared aggregate app resources exceed admission capacity and verify no
  registry/launcher changes. This is an explicit Phase 15 exit criterion;
  host admission rejection is already tested. Owner: isolated fixture/harness
  and controlled board session, without exhausting the actual hardware heap.

## Phase 16 — implementation and ABI closure

- [ ] **P16-1 — Complete the shared widget/property/event facade.** Build
  an exact implementation matrix against plan sections 6.1/6.2 before declaring
  the ABI complete. Static JSON widget creation and the resource ledger are
  not proof that every promised callable exists. Include handle validation,
  ownership and teardown. Document any intentional runtime-specific subset.
  Owner: implementation agent.
- [ ] **P16-2 — Dynamic list and chart operations.** Complete/test documented
  list add/insert/remove/clear/count and chart add-series/set-data/set-range/
  clear operations, rich-item behavior and events, invalid arguments and
  destroyed/cross-app handles. Current list/chart samples are static.
  Owner: implementation agent, then board examples.
- [ ] **P16-3 — Focus and keyboard.** Implement textarea focus, automatic
  keyboard show, programmatic focus, change/blur behavior, and safe keyboard
  dismissal/teardown when switching/closing the app or Settings. Keep visual
  styling coherent with the system. No keyboard in K is a limitation, not
  completed input support. Owner: implementation agent, then board validation.
- [ ] **P16-4 — Audit the rest of the promised facade.** Verify callable
  properties/text/value/visibility/enabled/style, scrolling/reparent/size,
  on/off events, timers/after and declared animation behavior. Implement
  missing operations or resolve contract differences before freeze; do not
  assume the remaining work is only three list/chart/focus functions.
  Owner: implementation agent.
- [ ] **P16-5 — Review and freeze ABI v1.** After the matrix and implementation
  are complete, review signatures, types, errors, permissions, ownership,
  bounds, lifecycle and versioning; freeze the contract with regression
  evidence. Phase 17's Lua sandbox/network implementation is not required
  here, but its promised interface must not be an unreviewed future guess.
  Owner: implementation agent/project review.

## Phase 16 — device evidence and quality follow-ups

- [x] **P16-T1 — Dashboard reconnect UI.** O confirms the instructed online/
  offline/reconnected UI sequence; serial supplies two disconnect/reconnect
  cycles. Together with K's time/timer observations this closes the missing
  basic service-binding UI observation. No new screenshot is claimed.
- [ ] **P16-T2 — Combined memory/network load.** Keep Dashboard running and
  panel enabled during a Weather/HTTPS request; capture before/TLS-peak/
  released internal-heap and PSRAM lines with a successful response. Existing
  Dashboard+TLS and panel+TLS captures come from different execution states.
  Include USB with Wi-Fi active in the remaining transport/load session.
  O adds panel+TLS success but Dashboard starts at 158666 ms, after both
  requests finish at 36422 ms. Internal free memory reaches 5807 bytes and
  the largest internal block reaches 3328 bytes at different samples. Resolve
  this limited headroom before the controlled combined-load test; no OOM is
  reported and separate execution states must not be combined as evidence.
  Owner: implementation agent for a reliable trigger, then user.
- [ ] **P16-T3 — Visible widget/facade coverage.** Demonstrate the hidden
  dialog open/button/dismiss flow, remaining visual variants/invalid input
  cases and the new dynamic/focus operations. Existing Status/Timer/Controls
  photos and user-reported static controls pass; they need not be repeated.
  Owner: implementation agent for fixtures, then user.
- [ ] **P16-Q1 — Startup timing.** Investigate recorded Dashboard onCreate
  times of 95–120 ms versus the current 80 ms warning. The plan's lifecycle
  guidance also permits standard apps 200 ms and calls budgets warnings.
  Choose/document the correct complexity class, measure, and optimize or
  adjust the warning accurately; this is not evidence of a crash, nor an
  automatic requirement that every Dashboard must finish below 80 ms.
  Owner: implementation agent.
- [x] **P16-Q2 — Launcher-caption device confirmation.** N runs ELF prefix
  07e71ed2e and confirms Action Guard... in the launcher with full Action Guard
  Check and Status Dashboard names in management views after upload/reboot.
  This completes the functional board confirmation of the source/real-LVGL
  fix. No new photograph or six-package boot transcript is claimed.

## Cross-cutting reliability and records

- [ ] **R1 — Resolve/classify cold-boot HTTPS failures.** Some runs time out or
  fail a handshake; others and later retries work, with no reported OOM in
  those failures. Retain failure/recovery evidence, investigate the actual
  cause, and decide whether it is a platform regression or an external
  network failure handled acceptably. Do not describe every network request
  as successful, or require remote servers never to fail. This inherited
  reliability follow-up is separate from a missing Phase 15/16 feature.
- [ ] **R2 — Final integrated regression/build and board batch.** Run targeted
  checks after the remaining implementation, then the final build and focused
  board checks. Preserve earlier valid evidence; no full A–M rerun is implied.
- [ ] **R3 — Final documentation, explicit dispositions, and closure.** Keep
  plan/code/design/overview checklists consistent, attach evidence and owners,
  record any agreed scope changes, then mark both phases closed only when
  their required items are implemented/validated. Commit/tag at the user's
  request. An unchecked administrative placeholder is not a missing feature.

## Suggested completion order

1. Settle exact catalog/browser transport/API scope in the documents; complete
   the Phase 16 facade matrix without asking the user to guess technical gaps.
2. Implement browser USB and robust shared logging; preserve P's functional
   browser-log pass and target only changed stream/concurrency behavior.
3. Complete facade/list/chart/focus and review startup classification.
4. Prepare one consolidated hardware batch covering new functionality,
   transport rejection/recovery, remaining token/expiry coverage, admission
   and combined-load headroom. N's caption and O's reconnect checks passed.
5. Complete hosted catalog evidence and ABI review, run final checks, reconcile
   all checklists, then close the phases.
