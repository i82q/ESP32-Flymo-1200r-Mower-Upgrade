# Status and open items

The single list of what's left. When an item is done, delete it here and record the result in the relevant reference doc. Tests that need the real installation are planned in `test_build_plan.md`; this file only points to them.

_Last tidied: 10 Oct 2026 (capture log 2–10 Oct reviewed; Skip Today written). 17 Oct: "No Mow" flag + mailbox design added; mower parked for new grass seed._

## Working now (firmware 25 Sep – 5 Oct)
- **AUX1 telemetry:** status, battery, tilt/collision, stats, blade and wheel telemetry. PIN login with auto-relogin.
- **0x12 decode:** `Automower State` (idle / mowing (auto|man) / going home / charging / leaving station / fault / waiting for loop signal), `Automower Mode` (HOME/AUTO/MAN), `Automower Timer` (no restriction / in work period / waiting), and `Automower Next Start` from the long 0x12 frame (confirmed 2 Oct).
- **AUX1 traffic gate:** traffic only while AUX1 is powered, from 20s after AUX1 power comes up (first-boot wizard guard), and paused automatically on Tools/EXP entry.
- **Keypad emulation:** all 15 keys via isolated relays. A single sequence engine with OK/Back/Start/hold characters; also reachable over MQTT `automower/keys`. Standby guard replays a sequence once if the mower sleeps mid-way (5 Oct).
- **Hatch emulation:** `Automower Hatch` switch holds the stop loop open; `[` / `]` in key sequences. Opening wakes the mower from standby; closing commits.
- **Mode control:** Mode Home/Auto/Man buttons play `[rmh1s]` / `[rmh2s]` / `[rmh3s]`. Mode Home is sticky and stays that way by choice (10 Oct: no automatic return to AUTO after a manual Home).
- **Mow-day tracking:** one summary per mowing day; docking inside the work period counts as a charge stop (cycles counted), docking outside it or with HOME selected ends the day. Last Mow Duration / Cutting Time / Blade Current / Faults / Summary, kept in flash; ntfy notification automation in HA. Worked Mon 5, Tue 6, Wed 7, Fri 9 Oct (1 cycle each; Wed's log lines were lost in an MQTT reconnect but the day did close — Fri logged a fresh "Mow day started").
- **Fault record decode:** `Automower Last Fault`, e.g. `code 15 Mower lifted` from the 0x18 record (9 Oct).
- **Adaptive polling + publish-on-change:** blade/wheels only while mowing, leaving or going home; tilt every 60s otherwise. Sensors publish only on change (small deadbands). Poll replies are logged only when they change.
- **Battery-fed ESP32:** pack-voltage ADC (`pack_multiplier` 7.57, confirmed 28 Sep; 7 Oct pairs down to 18.1V still within ~±0.15V, one −0.6V outlier under 2.1A load), AUX1-awake sense, reset diagnostics. Deep sleep only when pack < 17.5V **and** the mower is in standby.
- **Top-up wake:** wakes the mower, reports pack/mower voltage, charge current and clock drift. With HOME selected it doesn't charge (sleeps ~16 s after the lid closes). With AUTO selected (stopped) it does: 14 Oct 385 mA at 19.8 V, 15 Oct 1,037 mA at 19.0 V. Nightly Top-up was enabled from ~11 Oct.
- **Capture logging:** HA File notify + automation on `automower/#`. NB lines can be lost during MQTT reconnects on the lawn.
- **No unplanned ESP resets 2–10 Oct** (all reboots were OTA).

## Current state (17 Oct)
- **Mowing paused while new grass seed establishes.** Mower docked, **AUTO selected but stopped** (0x12 `01 00 05 00`), **timer cleared** (no restriction) — so a Start would send it straight out. This is the intended parked state for now: it charges on top-up wakes. Don't press Mode Auto until the timer is re-entered and the grass is ready.

## Design agreed 17 Oct — "No Mow" flag + command mailbox (next build)
Findings 13–17 Oct that drive it:
- **The mower's hatch rule:** a lid-close only counts as "closed" if Start was pressed while it was (logically) open. Close without Start → stays stopped (0x12 b2 = `05`). The worn stop-button mechanism makes this visible by hand: the microswitch recloses with the lid open, and the mower only accepts "closed" after Start + another open/close. Parts if wanted: micro switch **574 48 03-01**, spring **574 48 70-01**, spring lock **576 24 22-01**, lock **575 40 90-01**.
- **AUTO + stopped (b2 `05`) is the ideal "parked" state:** docked, won't mow, and a hatch wake keeps it awake long enough to charge. **HOME is not:** it sleeps ~16 s after a wake, so it never tops up.
- The 02:00 top-up wake (open/close, no Start) leaves it stopped — harmless now, but it would also disarm a mower that should mow.
- **Top-up skipped after an ESP restart** ("not docked" 16 and 17 Oct, last state unknown) → pack fell to 17.45 V and the ESP deep-slept (17 Oct 05:23). Must wake and check instead of skipping when the state is unknown.
- Timer cleared by hand on 13 Oct (b3 timer bits `0` = no restriction; settings counter bytes 20–21 jumped 0x129 → 0x165) — so **Start = mow immediately**. Re-enter the timer before re-arming.

The design:
- [ ] **`automower/no_mow` retained MQTT flag** (on/off), exposed in HA as a switch ("Mowing Enabled"). Retained so HA can change it while the ESP sleeps; applied at every wake.
  - **on:** if mowing → send home (`[rmh1s]`); once docked → `[rmh2]` + close **without Start** → AUTO selected, stopped, docked, tops up on every wake.
  - **off:** `[rmh2s]` → armed, follows the timer (restore the timer first if it's empty).
  - Every wake (top-up, sync, mailbox) presses Start **only if no_mow is off**.
  - Could replace HOME / Park / Skip Today for day-to-day use, and is what the rain automation would toggle.
- [ ] **`automower/queue` retained command mailbox:** HA posts a command (key sequence, `sync_time`, timer string…); on the next wake with AUX1 ready and the mower docked, the ESP runs it, publishes the outcome to `automower/queue_result`, then clears the retained message. Failed → keep it and retry next wake. Whitelist command types; never act while the mower is moving.
- [ ] **Top-up fixes:** run when the state is unknown after a restart (wake and check), and apply the no_mow rule for Start.
- [ ] **ESP sleeps with the mower** (wake on AUX1 via GPIO4 or every 15–30 min to check MQTT) — cuts the ~0.6 Ah/night drain; pairs naturally with the retained flag and mailbox.

## Check first
- [ ] **Flash the 10 Oct build (Skip Today + timer-check fix)** and test Skip Today → keypad_and_menus.md "Skip Today". (Superseded for now by the no_mow design — decide whether Skip Today stays.)
- [ ] **11:00 starts are still unreliable.** Mon 5 Oct: AUX1 woke 11:04, charging, back to standby 11:07, no departure. Tue 6 Oct: nothing at 11:00 (cause found 6 Oct: top-up/hatch wakes without Start had left it stopped, b4 `47`). Wed 7 Oct: on time. 12 Oct: woke 10:59:54 for 5 s and slept again — HOME was selected.
- [ ] **Who started the mower Fri 9 Oct 14:50?** Mode went from HOME (08) to AUTO (06) with no ESP key sequence. If that was by hand at the mower, fine; otherwise investigate.

## Built 10 Oct, to test
- [ ] **Skip Today** button + `Automower Skip` sensor: `[rmh1s]` now, `[rmh2s]` at 23:00 (`skip_rearm_*`), verified by mode bits, retried every 10 min ×6, cancelled by any mode button, pending flag in flash. Timer untouched. Test: press while docked → HOME; at 23:00 → AUTO + next start. Then once while mowing.
- [ ] **Restore Timer confirmation fix:** confirmed when a next start is shown (or in work period), `- HOME selected` noted. Park with HOME selected reports "can't be verified".
- [ ] Optional HA ntfy automation on `Automower Skip` containing "FAILED" / "NOT confirmed".
- [ ] Once Skip Today works: decide whether to retire Park / Restore Timer / Timer Auto Restore (Skip covers the day-to-day use without rewriting the timer).

## Built 2 Oct — Phase 0 results so far (test_build_plan.md)
- [ ] **Restore Timer, 9 Oct 23:00:** 6 attempts, all "not confirmed (timer bits 0x00)", then "FAILED" — but every attempt had written the timer: the long 0x12 frame showed Next Start = Mon 12 11:00 (b10 = 02) 2s after the lid closed, then 0 ~6s later. HOME was selected, which hides "waiting"/Next Start. Fixed 10 Oct (above); re-test with the new build.
- [ ] **Park (all work days off) 9 Oct 07:17: "not confirmed"** — inconclusive (HOME already selected). Still unproven; test with AUTO selected.
- [ ] **Keypad Self Test 5 Oct: FAIL** — step 1 HOME ok; step 2 got no reply because AUX1 dropped into standby mid-sequence (11:27:17). The 5 Oct standby guard should help. Also step 3 compares timer bits before/after, which HOME hides outside a work period — likely to false-fail; review before re-running.
- [ ] **Auto-wrap** of Send Keypad Menu Path and `automower/keys` as `[r … r s]` — sequences in the log are wrapped correctly; not separately tested.

## Firmware to do
- [ ] **Stuck detection.** Data from 28 Sep:
  - A lifted fault shows as 0x12 `07` + 0x14/04 lifted = 1, held for the whole fault (17:29:15–17:39:45). That combination is a reliable "stuck/lifted" alert.
  - A brief collision + lifted for 10s (18:03:35) recovered by itself, so only alert on the fault, not the flag alone.
  - A stall at 18:15 showed blade current ~1.5A just before the fault.
  - 9 Oct 15:36: code 15 "Mower lifted" while going home, ~1 min after two back-to-back `[rmh1s]` hatch sequences; recovered and docked by 15:37. Either the shell/bumper again or the hatch emulation while driving — avoid sending hatch sequences while the mower is moving.
  - Other triggers: fault + draining + off charger; AUX1 goes dark while out mowing without a home command; long `03` without finding the guide (test C1); state `08` lasting more than ~15 min.
- [ ] **Grass growth / skip-day tracking.** Needs a baseline of daily blade-current averages first (Last Mow Blade Current long-term stats). Watch for wet-grass effects. 28 Sep reference: ~2,900 rpm, current median 254mA, p95 459. Could later drive Skip Today automatically.
- [ ] **Rain → no_mow automation** once the rain gauge node works (separate ESP32 + CC1101 decoding the TFA DROP 30.3233.01; `rain-gauge.yaml`). The RFXtrx433E (fw 1043/1048 Pro1) receives the DROP but doesn't decode it; an RXB6 receiver was too noisy at 10 m.

## Next yaml round
- [ ] **HA mower entity via MQTT lawn mower.** Lets the built-in Tile card (or a custom mower card) drive the mower, and gives a proper `lawn_mower` entity for automations. Design agreed 5 Oct:
  - ESP publishes `automower/activity` (retained): `mowing` (02/05), `returning` (03), `docked` (04, or 01 after docking), `paused` (hatch held open off the dock), `idle` (01 off the dock), `error` (07/08).
  - HA commands on one topic, `automower/mower`, with HA's default payloads: `start_mowing` → no_mow off, `pause` → hatch open, `dock` → no_mow on (see 17 Oct design). Consider AUX1 Set mode 0x02 for dock (see Protocol below).
  - HA side: an `mqtt: lawn_mower:` block using those topics and `automower/status` for availability.
  - Then hide most of the button clutter from the device page.
- [ ] **Rename the ESPHome `friendly_name`** from "Automower AUX1 Bridge" to something shorter and more useful (e.g. "Automower"). Check how HA displays entity names that already start with "Automower" before choosing, and that existing entity IDs (used by automations) stay unchanged.
- [ ] **Tidy-up (no behaviour change):** remove the dead globals `tc_park` and `tu_manual`; move the "Changes 5 Oct / 10 Oct" header comments into the docs; factor the repeated "publish Next Start" code (now ×5) and battery read (×2) into helpers in `aux1_protocol.h`; replace the key-language comment block with a pointer to `keypad_and_menus.md`.
- [ ] **Optional cuts, to decide then:** the 17 single-key buttons (Keypad Menu Path with `!` covers them); Status Byte3/Byte4 numeric sensors; the `pair:` voltage log line once the low-voltage pairs are in.
- [ ] Project file naming: the device's file is `mower.yaml` in the ESPHome builder; the project keeps it as `automower.yaml`.

## Hardware / installation
- [ ] **WiFi.** Still weak: roam scans at −84 to −92 dBm while mowing and docked, regular MQTT offline/online drops (and lost log lines). No resets since 2 Oct though. Improve AP placement or fit an external antenna. Needed before the test session (flight-recorder dumps are bursty).
- [ ] **Bumper/shell sticking lifted** (mechanical): stuck twice in one mow on 28 Sep; possible repeat 9 Oct 15:36. Check the shell moves freely on its mounts.
- [ ] **Stop button / lid microswitch worn:** recloses with the lid open (17 Oct). Parts above.

## Answered by the test session (test_build_plan.md)
- Can any AUX1 command clear a fault? (B5)
- Is the `s` needed in `[rmhNs]`, or does the hatch close commit on its own? (A1) — **answered 17 Oct by hand: Start is needed; a close without Start leaves it stopped.**
- Does AUX1 Home / Search work without the keypad? (A2, B9) — **strong hint 9 Oct:** AUX1 `Set mode 0x02 -> status 0x00` at 15:34:31 and the next status poll (15:34:36) already showed `03` going home with mode 08, *before* the `[rmh1s]` keypad sequence started at 15:34:40. Confirm deliberately.
- Loop and guide failures L1–L4 (A3, B6, B6b, B8, C1) — screen message, frames before the fault, error counter, 0x18 event record.
- Stop on the lawn: distinct from idle? (B3)
- ECO while charging: does b1 change when the loop is off? (C3)
- b1 bits 1–3 vs position relative to the guide/boundary wires (A2 P1–P7).
- Tilt Z axis and the wrong-sign left-roll point (A2 Z1–Z7).
- Why the blade stops mid-mow (28 Sep 17:47, 29 Sep 13:17) — flight recorder in B1/B2.
- Is power loss logged? Wake timing vs `wake_wait_ms` (D1, D2).

## Keypad / menu unknowns → test_build_plan.md Phase E
- Does a setting commit on the final digit, or only on exit?
- Work days digit order Mon=1…Sun=7 (verify via Next Start). 9 Oct restore `[… rm13867 …]` gave Next Start Mon 12 11:00 from Fri night, consistent with 6/7 = Sat/Sun off.
- Remote start option order; Security submenu shortcuts; Work hours 2 / Reset timer screens.

## Protocol unknowns
- b1 bit2 (candidate "boundary wire close by") and bits 1–3 generally (A2).
- b2 `05` = stopped / not armed (after STOP or a lid-close without Start); `06`/`07`/`08` = armed AUTO/MAN/HOME.
- b3 with HOME: `20` outside a work period (never `22` "waiting"), `21` inside one (5 Oct); `22` seen only while the lid was open mid-sequence. Next Start is shown for a few seconds after the lid closes, then 0.
- b4 values: `24` keypad in use, `46` parked by the timer (armed), `47` stopped/won't start (seen before standby after a lid-close without Start, and with HOME), `1B` once after the 0x10 incident, `27` before a re-login. Any others?
- Long 0x12 frame: byte 18, bytes 20–21 (looks like a settings-write counter: 0x0119 → 0x0129 → 0x0154 over 5–9 Oct, → 0x0165 on 13 Oct after the timer was cleared by hand), the u16 after total hours.
- Short 0x12 reply `02 13 01 03 88 03` (status 0x03, no payload) seen 5 Oct 13:45 and 7 Oct 10:48 while driving home — status 03 is the "menu open / busy" reply; the parser already ignores it. Why while going home?
- 0x18 event records (`[05 01]`, `[06 01]`, `[0C]`): map the codes. `02 00 02 FF 01 12` ≈ no loop signal. Read `[05 01]` after every fault.
- Wheel bytes 4–5 / 10–11, blade bytes 4–5.
- Stats: confirm mow/search hours against the display; bytes 6–7; production date.
- The status-05 commands (`08 0A 1C 24 26 2A 2C 2E`): a service PIN, or unimplemented?
- Undecoded: 0x16 details, 0x28, 0x02 b2/b5, status codes 01/04.
- Fault codes: capture the screen text + frames whenever a fault occurs. Known: 2 = No loop signal, 15 = Mower lifted; 19 likely "Collision sensor problem" (5 Oct trampoline, screen not confirmed).
- An AUX1-visible signal for the first-boot wizard (not planned — too risky).
- Measure AUX1 pin 7 (~18V).
- **Never sweep 0x10 or 0x36** — they are actions, not reads.

## Housekeeping
- [ ] Purge stale retained MQTT topics on mqtt.home. With `topic_prefix: null` nothing refreshes the old `automower/sensor|switch|select|text/...` topics, so clear them (`mosquitto_pub -r -n`). That includes `automower_command`, `automower_stop_loop`, the `keypad_*` switches and any `homeassistant/…/automower` discovery configs. They still replay on every HA restart (seen 13 Oct).
- [ ] Delete leftover `zigbee2mqtt/…` retained topics on the old broker (192.168.42.5), if any.
- [ ] Move Solar Assistant to the official HACS integration, then remove the MQTT bridge entirely.
- [ ] Capture log: ~1MB per mowing day, never rotates, and sits inside /config (so it's in backups). Reached 1.8MB by 17 Oct. Stop or clear it after the test session.
- [ ] Update the `Automower Tilt Z` comment in the yaml ("Z-axis or magnitude - unconfirmed") once the Z tests are done.
