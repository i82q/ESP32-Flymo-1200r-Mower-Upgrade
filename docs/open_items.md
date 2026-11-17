# Status and open items

The single list of what's left. When an item is done, delete it here and record the result in the relevant reference doc. Tests that need the real installation are planned in `test_build_plan.md`; this file only points to them.

_Last tidied: 17 Nov 2026 (controls tidied, Park/Mow switch + run_command — branch). 10 Oct 2026 (capture log 2–10 Oct reviewed; Skip Today written). 17 Oct: "No Mow" flag + mailbox design added; mower parked for new grass seed. 3 Nov: capture log 10 Oct – 3 Nov reviewed (see "Log review 10 Oct – 3 Nov" below). 4 Nov: unexplained stop on the lawn — evidence log under Hardware. 8 Nov: capture log 3–8 Nov reviewed — Park clears the timer, no-loop fault 7 Nov._

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
- **Battery-fed ESP32:** pack-voltage ADC (`pack_multiplier` 7.57, confirmed 28 Sep; 7 Oct pairs down to 18.1V still within ~±0.15V, one −0.6V outlier under 2.1A load; 2,404 pairs 2 Oct – 3 Nov down to 17.56V: median −0.003V, but the ADC reads ~+0.2V high while charging at ~1A and ~−0.1V low while discharging), AUX1-awake sense, reset diagnostics. Deep sleep only when pack < 17.5V **and** the mower is in standby.
- **Top-up wake:** wakes the mower, reports pack/mower voltage, charge current and clock drift. With HOME selected it doesn't charge (sleeps ~16 s after the lid closes). With AUTO selected (stopped) it does: 14 Oct 385 mA at 19.8 V, 15 Oct 1,037 mA at 19.0 V. Nightly Top-up was enabled from ~11 Oct, left off by choice 19 Oct – 2 Nov, ran again 3 Nov. With AUTO **armed** and the timer waiting (3 Nov 02:00, pack 19.7 V, state `01 00 05 02`) it did **not** charge.
- **Capture logging:** HA File notify + automation on `automower/#`. NB lines can be lost during MQTT reconnects on the lawn.
- **No unplanned ESP resets 2 Oct – 3 Nov** (all reboots were OTA or low-pack deep-sleep wakes; ESPHome 2026.10.0 since 22 Oct). The 21 "task watchdog" reset reasons in the capture log are stale retained topics replayed on HA restarts (uptime always 1562) — see Housekeeping.

## Current state (17 Nov 13:30)
- **Parked: AUTO selected, lid closed without Start, charging** (`04 0C 05 00`, 1.11 A, 18.4 → 18.7 V in 12 min). Set by hand at 12:58. Timer still has all work days off from the 13 Nov Park (= no restriction).
- **8–17 Nov:** mowed on the timer 9–13 Nov (2 cycles/day; 9 Nov 11:14 code 15 "Mower lifted", recovered). 13 Nov 13:20 Park pressed mid-mow with HOME selected → docked in HOME. Pack down to 17.5 V by 15 Nov 20:31; the ESP then woke hourly and slept again (42 times). **In HOME the mower never charged** — the manual says HOME stays docked, nothing about topping up.
- **What kept it charged in October** (13 Oct – 2 Nov, same AUTO-without-Start state): the mower never woke by itself. Every wake was the 02:00 top-up or the ESP coming out of its 1 h low-pack sleep (mower awake 57–59 min after each sleep started: the ESP boot briefly opens the stop loop). Each wake charged it to full (~1.1 A, 17.8 → 20.3 V in ~2 h; 45–68 min from ~19 V). So: **parked = AUTO, lid closed without Start, Nightly Top-up on.** In HOME the same wakes do nothing (sleeps after ~16 s) — this week's hourly ESP boots show exactly that.
- Unexplained: two short wakes 22 Oct 18:55 and 19:14 (~7 min each, no charging, no ESP action).

## Earlier state (8 Nov 11:23)
- **Out of action since Sat 7 Nov 17:58: no loop signal, not charging.** It left the station at 17:58 (`05 0F`), went straight to state `08` "waiting for loop signal" (b1 `00`, off the charger — probably just in front of the station) and logged **fault code 2 "No loop signal"** at 17:59 (0x18 record `E9 66 EF 6A 02 00 02 FD 00 12 …`). Since then AUX1 wakes for ~35 s every ~5.5 min to look for the signal; status alternates `08 00 06 00 47` / `08 00 06 00 00`.
- **When the loop was lost:** after 16:58 (it docked by following the guide wire, so the signal was there) and before 17:58. The station still had power at 17:53 (charging ~393 mA, pack 20.48 V). So a power cut is unlikely — suspect a **boundary/guide wire break** or the station's loop output. Check the station LED, then the wire.
- **Battery:** 20.48 V (17:53 Sat) → 19.25 V (11:00 Sun), ~−90 mA, ~0.06 V/h. At that rate it reaches the ESP's 17.5 V deep-sleep threshold around **Mon 9 Nov evening** — fix before then.
- **Resolved 8 Nov ~11:40:** loop broken (probably damp, temporary fixes made); mower carried back to the charger by hand, charging, Mode Home pressed in HA (sticky — press Auto, with the timer restored, to mow again). The notes below are kept for the record.
- **Before the loop comes back, select HOME or Restore Timer:** the timer has no restriction (see Park below), so once the signal returns it would go mowing straight away, day or night.

## Earlier state (4–7 Nov)
- **7 Nov (Sat):** AUTO selected by hand at the mower at ~11:47 (AUX1 off/on 4 s apart, no ESP key sequence; status `04 0C 06 00 00` = armed, timer bits `0` no restriction, Next Start 0). It then mowed whenever charged, Saturday or not: 12:49–14:22 (16 min of guide/home search at the end), 15:30–16:58, and out again at **17:58 in the dark** → no-loop fault (above). Without the fault it would likely have kept going overnight.
- **6 Nov 00:30:** Automower Park pressed: `[rm1381234567brs]`; "park sent; HOME selected, so the timer state can't be read".
- **5 Nov:** nothing (HOME selected).
- **4 Nov:** left on the timer at 11:00:34 (first timer start after the clock sync — on time). Cycle 1 11:00–12:07, charge 75 min, cycle 2 from 13:23 → **stopped by itself on the lawn at 14:09** (see "Unexplained stops" under Hardware). Found sitting there; sent home by hand at 15:54 with **HOME selected** (b2 `08`, b3 `20`) — set AUTO again before the next timer start.

## Earlier state (3 Nov)
- **Mowing resumed.** 2 Nov ~11:28 started by hand at the mower (no ESP key sequence; b2 `05`→`06`), one cycle. Normal timer re-entered 2 Nov 14:30 with the keypad path `[rm1111001530orm1200000000orm13867brs]` → Next Start 3 Nov 11:00.
- **3 Nov:** left on the timer at 11:06, 2 cycles (11:07–12:32, charge 82 min, 13:55–15:16), lowest pack 17.56 V at the end of cycle 1.
- Mower clock synced 3 Nov 02:00 (it had been +1 h since the BST change on 25 Oct).
- Before the 2 Nov restore, Next Start read 3 Nov 01:00 (mower clock) with the timer cleared — it looks like it would have gone out in the night. Re-enter the timer whenever mowing resumes after a clear.

## Log review 10 Oct – 3 Nov
- **Low-pack deep sleep happened 7 times** (13, 17, 20, 22, 24, 27, 30 Oct; pack 17.32–17.50 V) while Nightly Top-up was off. Every wake from 17 Oct on found the mower **already awake and charging** ~1.05 A from ~17.9 V, reaching ~20.3 V in 2–3 h, then standby. So the pack cycled full → 17.4 V roughly every 2–3 days without any top-up.
- **Open question:** what woke the mower during that hour? Either the relay release on the way into deep sleep (`release_keys` + stop loop off) acts as a lid open/close, or the mower starts its own charge at low voltage. Decides whether the 17.5 V deep sleep is a dependable floor. Test: with the ESP awake and logging, let the pack run below 17.5 V with deep sleep disabled and see if AUX1 powers up by itself.
- **b2 `05` is not "won't start".** After the 2 Nov 14:31 timer restore and the 3 Nov 02:00 top-up (lid open/close, no Start), b2 was `05` with b3 `02` waiting and b4 `00`/`24` — yet it left on the timer at 11:06 (b2 `05`→`06` on departure). The 6 Oct no-start had b4 `47`, so **b4 `47`** looks like the real "stopped, won't start" marker. This weakens the 17 Oct hatch rule (below) — check before building no_mow on "close without Start parks it".
- **Clock drift / BST:** the mower doesn't change for summer time; the ESP only syncs it during a top-up, so with Nightly Top-up off it ran an hour fast for 9 days. → Firmware to do.
- AUX1: 4 brief RX timeouts / one CMD mismatch (9, 25, 30 Oct), each recovered on its own, some with an auto re-login. 7–8 Nov: a handful more (19:32, 22:42–22:48, 06:39), all during the short no-loop wakes as AUX1 powered down mid-poll; harmless.
- While mowing, preferences are written to flash every minute (mow-day tracking). Fine for NVS wear, but easy to cut if tidying.

## Design agreed 17 Oct — "No Mow" flag + command mailbox (next build)
Findings 13–17 Oct that drive it:
- **The mower's hatch rule:** a lid-close only counts as "closed" if Start was pressed while it was (logically) open. Close without Start → stays stopped (0x12 b2 = `05`). **3 Nov caveat:** b2 `05` with the timer waiting still left on schedule — see "Log review 10 Oct – 3 Nov". The worn stop-button mechanism makes this visible by hand: the microswitch recloses with the lid open, and the mower only accepts "closed" after Start + another open/close. Parts if wanted: micro switch **574 48 03-01**, spring **574 48 70-01**, spring lock **576 24 22-01**, lock **575 40 90-01**.
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
- [x] **Top-up after a restart** — fixed 17 Nov (`docked_known`). The no_mow flag / mailbox were dropped in favour of the Mowing switch + `run_command` (17 Nov).
- [ ] **ESP sleeps with the mower** (wake on AUX1 via GPIO4 or every 15–30 min to check MQTT) — cuts the ~0.6 Ah/night drain; pairs naturally with the retained flag and mailbox.

## Built 17 Nov, to test (branch `park-switch-and-tidy`)
Not compiled here (no PlatformIO access): `esphome config` passes, all lambdas compile on the host against stub ESPHome types, and the state decoder was checked against real 0x12 frames.
- **`run_command`**: plays `keys>expected; ...` steps, skipping a step whose state is already met, checking each result from 0x12 — see keypad_and_menus.md "Commands". Keypad Menu Path and MQTT `automower/keys` use it too.
- **Automower Mowing** switch: off = `cmd_park` (`[rmh1s]>docked; [rmh2]>auto_stopped`), on = `cmd_mow` (`[rmh2s]>auto_armed`). Timer untouched.
- Nightly top-up no longer skips "not docked" after an ESP restart (`docked_known`, kept in flash).
- Removed: see keypad_and_menus.md "Home Assistant controls". The old `mowing-switch-and-mailbox` branch is superseded (GitHub refused the delete from here — delete it by hand).

Tests:
- [ ] Flash while docked and charging. **Turn Nightly Top-up on.**
- [ ] Mowing off while docked → `[rmh1s]>docked` skipped, `[rmh2]>auto_stopped ok`. Confirms `[rmh2]` (no Start) selects AUTO — never tried by the ESP before.
- [ ] Next mornings: 02:00 top-up charges it (`Automower Top-up`: charging … mA).
- [ ] **Does parked hold with the normal timer set?** Restore the timer by hand (or Keypad Menu Path `m1111001530o r m1200000000o r m13867b`), park, watch the next 11:00. 6 Oct: stopped + timer → didn't start (b4 `47`); 3 Nov: stopped (b4 `00`/`24`) + timer → left at 11:06. Until this is known, keep the timer empty while parked.
- [ ] Mowing on → `[rmh2s]>auto_armed ok`, leaves at the next start.
- [ ] Mowing off while it's out → HOME + Start, docks, then AUTO without Start.
- [ ] Mowing off while stopped on the lawn (like 4 Nov) → `docked` not met (last seen moving) → sent home.

## Check first
- [ ] **Flash the 10 Oct build (Skip Today + timer-check fix)** and test Skip Today → keypad_and_menus.md "Skip Today". (Superseded for now by the no_mow design — decide whether Skip Today stays.)
- [ ] **11:00 starts are still unreliable.** Mon 5 Oct: AUX1 woke 11:04, charging, back to standby 11:07, no departure. Tue 6 Oct: nothing at 11:00 (cause found 6 Oct: top-up/hatch wakes without Start had left it stopped, b4 `47`). Wed 7 Oct: on time. 12 Oct: woke 10:59:54 for 5 s and slept again — HOME was selected.
- [ ] **Who started the mower Fri 9 Oct 14:50?** Mode went from HOME (08) to AUTO (06) with no ESP key sequence. If that was by hand at the mower, fine; otherwise investigate.

## Built 2 Oct — Phase 0 results so far (test_build_plan.md)
- [x] **Park (all work days off) — answered 7 Nov: it does NOT park, it removes the schedule.** After the 6 Nov park, AUTO showed timer bits `0` (no restriction, same as the 13 Oct cleared timer) and the mower went out every time it was charged, including after dark. **Retire Park / Timer Auto Restore**, or rebuild "parked" on the 17 Oct design (AUTO selected, stopped, no Start). Never leave the mower in AUTO after a Park.
- [ ] **Keypad Self Test 5 Oct: FAIL** — step 1 HOME ok; step 2 got no reply because AUX1 dropped into standby mid-sequence (11:27:17). The 5 Oct standby guard should help. Also step 3 compares timer bits before/after, which HOME hides outside a work period — likely to false-fail; review before re-running.
- [ ] **Auto-wrap** of Send Keypad Menu Path and `automower/keys` as `[r … r s]` — sequences in the log are wrapped correctly; not separately tested.

## Firmware to do
- [ ] **Clock sync independent of Nightly Top-up.** Sync the mower clock on any wake with AUX1 ready and the mower docked (mow start, charge, manual wake), not only in the top-up — and/or a one-off wake the night after a BST change. 25 Oct – 3 Nov it ran +1 h because top-up was off. Fits the mailbox `sync_time` command.
- [ ] **Stuck detection.** Data from 28 Sep:
  - A lifted fault shows as 0x12 `07` + 0x14/04 lifted = 1, held for the whole fault (17:29:15–17:39:45). That combination is a reliable "stuck/lifted" alert.
  - A brief collision + lifted for 10s (18:03:35) recovered by itself, so only alert on the fault, not the flag alone.
  - A stall at 18:15 showed blade current ~1.5A just before the fault.
  - 9 Oct 15:36: code 15 "Mower lifted" while going home, ~1 min after two back-to-back `[rmh1s]` hatch sequences; recovered and docked by 15:37. Either the shell/bumper again or the hatch emulation while driving — avoid sending hatch sequences while the mower is moving.
  - **Stopped on the lawn (4 Nov 14:09):** 0x12 b0 `01`, b1 `00` (not on the charger), b2 `05`, b4 `24`, inside a work period, no fault — sat there 1 h 45 min unnoticed. Alert after ~5 min in that state. Nothing else in 0x12/0x14 flags it.
  - **No loop signal (7 Nov 17:59):** state `08` + fault code 2 right after leaving the station, then periodic ~35 s wakes every ~5.5 min while off the charger and draining (~−90 mA). Alert at once on `08` + code 2, and again if the pack keeps falling.
  - Other triggers: fault + draining + off charger; AUX1 goes dark while out mowing without a home command; long `03` without finding the guide (test C1); state `08` lasting more than ~15 min.
- [ ] **Unexplained-stop capture** (evidence for "Unexplained stops" below). When b2 goes `06`/`07` → `05` while off the charger (b1 bit3 clear) and the ESP sent no key sequence or hatch change in the last ~30 s: log `WARN stop: unexplained` with the last ~60 s of 0x14/02/03/04 frames and the 0x12 frame (keep a small ring buffer), publish to an `Automower Last Stop` text sensor, and fire the ntfy automation. Also count them (`Automower Unexplained Stops`, kept in flash). Optionally poll 0x12 every 10 s while mowing (now 30 s) to time the stop more tightly.
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
- [ ] **Optional cuts:** the `pair:` voltage log line (low-voltage pairs are now in, down to 17.56 V — can go). (Single-key buttons and Status Byte3/Byte4 removed 17 Nov.)
- [ ] Project file naming: the device's file is `mower.yaml` in the ESPHome builder; the project keeps it as `automower.yaml`.

## Hardware / installation
- [ ] **WiFi.** Still weak: roam scans at −84 to −92 dBm while mowing and docked, regular MQTT offline/online drops (and lost log lines) — 370 offline events 2 Oct – 3 Nov, roam scans mostly −84 to −92 dBm. No resets since 2 Oct though. Improve AP placement or fit an external antenna. Needed before the test session (flight-recorder dumps are bursty).
- [ ] **Loop wire break (7 Nov, signal lost 16:58–17:58)** — probably damp getting into a joint; temporary fixes made 8 Nov. Make a permanent repair (gel-filled connectors) and watch for a repeat in wet weather.
- [ ] **Bumper/shell sticking lifted** (mechanical): stuck twice in one mow on 28 Sep; possible repeat 9 Oct 15:36. Check the shell moves freely on its mounts.
- [ ] **Stop button / lid microswitch worn:** recloses with the lid open (17 Oct). Parts above.
- [ ] **Unexplained stops mid-mow — keep watching, collect evidence.** Suspect: the stop loop opening briefly while driving. Two things are in that loop: the worn lid microswitch, and the ESP's B2R8 NC relay contact + its splice. The log can't tell them apart.
  - **What a case looks like (4 Nov 14:09:04–14:09:14, 10 s poll gap):** everything normal up to the last poll — blade 2,894 rpm / 208 mA (max 566 mA in the 5 min before, no stall spike), wheels straight ~295 mm/s / 73 %, level (X +1.6°, Y +1.0°), no collision/lift flag, pack 19.03 V under load, 0x12 unchanged since 13:23. Next poll: blade 0 rpm / −12 mA, wheels 0, tilt frozen; 0x12 → `01 00 05 01 24`; no fault, 0x18 record unchanged. ESP up and connected throughout (no reset, no key sequence, Hatch untouched) — so not a relay drop on reboot. The blade "unknown" field (normally 5816) jumping at 14:07:24/14:09:24 is noise (~12 % of mowing samples do that).
  - **Bench checks:** mower awake on the dock, lid shut, AUTO — flex the wiring at the relay board and the splice into the stop loop, then tap/press around the lid and the stop-button mechanism; whichever stops it is the culprit. Check the B2R8 NC contact for continuity while tapping the relay board. Inspect the crimps.
  - **Each new case, record:** date/time, where on the lawn (slope? bumpy? near the boundary?), wet/dry, cycle and minutes into it, pack V, whether the lid looked fully shut, and what the screen showed when found. Add a row below.
  - **Next step if it repeats:** fit the microswitch 574 48 03-01 (+ spring 574 48 70-01) and re-make the splice — doing both at once won't say which it was, so do the bench checks first.

  | Date / time | Cycle, min in | Where / conditions | Pack | Notes |
  |---|---|---|---|---|
  | 4 Nov 14:09 | 2, 46 min | ? (driving straight, level) | 19.0 V | No fault, b4 `24`. Found by hand, sent home 15:54 with HOME. |

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
- b2 `05` = stopped / not armed (after STOP or a lid-close without Start); `06`/`07`/`08` = armed AUTO/MAN/HOME. **But** 3 Nov: `05` with b3 `02` (waiting) and b4 `00`/`24` still started on the timer — b4 `47` may be the real "won't start" flag. Pin down which combination blocks a timer start.
- b3 with HOME: `20` outside a work period (never `22` "waiting"), `21` inside one (5 Oct); `22` seen only while the lid was open mid-sequence. Next Start is shown for a few seconds after the lid closes, then 0.
- b4 values: `24` keypad in use, `46` parked by the timer (armed), `47` stopped/won't start (seen before standby after a lid-close without Start, and with HOME), `1B` once after the 0x10 incident, `27` before a re-login. Any others?
- Long 0x12 frame: byte 18, bytes 20–21 (looks like a settings-write counter: 0x0119 → 0x0129 → 0x0154 over 5–9 Oct, → 0x0165 on 13 Oct after the timer was cleared by hand), the u16 after total hours.
- Short 0x12 reply `02 13 01 03 88 03` (status 0x03, no payload) seen 5 Oct 13:45 and 7 Oct 10:48 while driving home — status 03 is the "menu open / busy" reply; the parser already ignores it. Why while going home?
- 0x18 event records (`[05 01]`, `[06 01]`, `[0C]`): map the codes. `02 00 02 FF 01 12` ≈ no loop signal. Read `[05 01]` after every fault.
- Wheel bytes 4–5 / 10–11, blade bytes 4–5.
- Stats: confirm mow/search hours against the display; bytes 6–7; production date.
- The status-05 commands (`08 0A 1C 24 26 2A 2C 2E`): a service PIN, or unimplemented?
- Undecoded: 0x16 details, 0x28, 0x02 b2/b5, status codes 01/04.
- Fault codes: capture the screen text + frames whenever a fault occurs. Known: 2 = No loop signal (also 7 Nov 17:59, record `E9 66 EF 6A 02 00 02 FD 00 12`), 15 = Mower lifted; 19 likely "Collision sensor problem" (5 Oct trampoline, screen not confirmed).
- An AUX1-visible signal for the first-boot wizard (not planned — too risky).
- Measure AUX1 pin 7 (~18V).
- **Never sweep 0x10 or 0x36** — they are actions, not reads.

## Housekeeping
- [ ] Purge stale retained MQTT topics on mqtt.home. With `topic_prefix: null` nothing refreshes the old `automower/sensor|switch|select|text/...` topics, so clear them (`mosquitto_pub -r -n`). That includes `automower_command`, `automower_stop_loop`, the `keypad_*` switches and any `homeassistant/…/automower` discovery configs. They still replay on every HA restart (seen 13 Oct; 21 replays 3 Oct – 1 Nov, each showing a bogus "task watchdog" reset reason and uptime 1562).
- [ ] Delete leftover `zigbee2mqtt/…` retained topics on the old broker (192.168.42.5), if any.
- [ ] Move Solar Assistant to the official HACS integration, then remove the MQTT bridge entirely.
- [ ] Capture log: ~1MB per mowing day, never rotates, and sits inside /config (so it's in backups). Reached 1.8MB by 17 Oct, 3.2MB by 3 Nov. Stop or clear it after the test session.
- [ ] Update the `Automower Tilt Z` comment in the yaml ("Z-axis or magnitude - unconfirmed") once the Z tests are done.
