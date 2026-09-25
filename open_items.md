# Status and open items

The single list of what's left. When an item is done, delete it here and record the result in the relevant reference doc.

## Working now (firmware rewrite flashed 25 Sep 2026)
- **AUX1 telemetry:** status, battery, tilt/collision, stats, plus blade and wheel telemetry. PIN login with auto-relogin.
- **AUX1 traffic gate:** traffic only while AUX1 is powered, starting 20s after AUX1 power comes up (first-boot wizard guard), and paused automatically on Tools/EXP entry.
- **Keypad emulation:** all 15 keys via isolated relays. A single sequence engine with OK/Back/Start/hold characters; also reachable over MQTT.
- **Stop-loop pulse:** relay NC contact; wakes the mower from standby.
- **Battery-fed ESP32:** pack-voltage ADC (averaged), AUX1-awake sense, low-battery deep sleep that releases all relays first, brownout/reset diagnostics.

## Next milestone
- [ ] **Test "Automower Start Auto"** (AUX1 auto + keypad Start). Does the mower actually move?
  - Watch the wheel power/speed sensors and `Automower State`.
  - If it doesn't move, try the other order (Start first, then AUX1) or `start`/`manual`.
- [ ] Confirm what happens after a stop-loop wake: does the Timer resume on its own, or does it need an explicit start?
- [ ] Recheck whether the mower now enters standby normally with the relay NC contact (it stopped doing so with the 4066). `Automower State` = `standby` shows it.
- [ ] **Settle `pack_multiplier`:** 7.57 in the yaml vs 6.972 from the 18 Sep calibration. Compare `Pack Voltage` with `Battery Voltage` at the same moment.
- [ ] **Check the wizard guard in the log:** after a battery swap, confirm "AUX1 powered - holding off traffic for 20s" appears before the first TX.
- [ ] Reassemble the mower, then field-test the whole build together.

## State discovery (0x12 payload byte0; only 01/04/07 known)
Any new code now shows up directly as `unknown 0xNN` in `Automower State`.

| # | Test | Question |
|---|---|---|
| 1 | Bare idle off base | Sanity check: is it 01? |
| 2 | Stop on lawn (0x0E 00 + keypad) | Is it distinct from idle, or is 01 a catch-all? |
| 3 | Auto, real movement | New mowing code? |
| 4 | Manual | Same as auto, or distinct? |
| 5 | Home / returning (poll throughout) | Searching state(s)? |
| 6 | Search mode alone | Distinct from home? |
| 7 | Docked, charge complete | Does it stay 04 once the current is ~0? |
| 8 | Loop connected vs cut | Where is `LOOP_ON`? |
| 9 | Loop cut mid-run | Distinct code, or just 07? |
| 10 | Base mains pulled while docked | Is it distinguishable from a loop break? Check the fault log after. |
| 11 | Timer end, parked-by-timer | Distinct code? |
| 12 | Raw-query the gated/undecoded cmds on different screens | Any hint of screen state? (Speculative) |

**Garage test loop (for tests 3, 5 and 6):**
- Guide wire: at least 2m straight from the station, at least 30cm from the boundary, and bends of 135° or less.
- Loop size: about 2.5–3 × 2m. The boundary corners can be 90°.

## Features to build
- [ ] **Stuck detection.** Two triggers:
  - Fault active + draining + off charger.
  - AUX1 goes dark while out mowing without a home command having been sent.
  - Needs the raw 0x12 fault bytes for deliberate wheel-stuck and blade-stuck cases.
- [ ] **Roaming / no-grass detection.**
  - Rolling average of blade current over 30–60s while mowing.
  - Below the threshold for several minutes → send home with `r`.
  - Optionally cross-check wheel telemetry for wedged/pivoting.
  - Blade current is now polled every 10s; log a real outdoor mow first (thick grass, thin grass, bare ground, wire crossings) to set the threshold. Also check whether RPM sags under load or only the current changes.
- [ ] **Low-battery return threshold.** Log voltage and capacity through a real mow-and-return and note the transition point.
- [ ] **Polling strategy.** Slow the 10s motor/tilt poll when idle or charging, keep full rate when active or faulted.

## Keypad / menu unknowns
- Does a setting commit on the final digit, or only on exit? Check each screen type with an AUX1 read in between.
- Security submenu shortcuts. Screens with more than 2 options (e.g. Remote start). Work hours 2 / Reset timer.
- Verify the Work days digit order is Mon=1…Sun=7.

## Protocol unknowns
- Other 0x12 byte0 codes (see the table above).
- b3 alternating 01/02; the exact behaviour of b4.
- Tilt: recheck the wrong-sign left-roll point; is bytes 6–7 a Z-axis? (X²+Y²+Z² ≈ constant across a single-axis sweep would confirm it.)
- Wheel bytes 4–5 / 10–11, blade bytes 4–5.
- Stats: confirm mow/search hours against the display; bytes 6–7; production date.
- The status-05 commands (`08 0A 1C 24 26 2A 2C 2E`): a service PIN, or unimplemented?
- Undecoded: 0x00, 0x30, 0x16, 0x02 b2/b5, and status codes 01/04.
- Timer status (Active/Disabled/Standby); reading back the schedule.
- An AUX1-visible signal for the first-boot wizard (planned: capture logs across a factory reset).
- Fault codes: capture the screen text + 0x12 payload whenever 53 or the transient loop fault recurs.
- Check the fault log after a deliberate battery pull (is power loss logged?).
- Measure pin 7 (~18V).

## Housekeeping
- [ ] In HA, remove the orphaned `Automower Command` select entity.
- [ ] Purge stale retained MQTT topics: the old `keypad_test_cycle_*` entities, plus any `homeassistant/…/automower` discovery configs left over from before `discovery: false`.
