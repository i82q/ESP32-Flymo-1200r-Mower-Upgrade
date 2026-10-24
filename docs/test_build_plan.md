# Test plan

_Last reviewed: 5 Oct 2026._

Two parts:
- **Phase 0** runs on the everyday firmware, docked, and can be done now. It tests the timer-control and self-test features built on 2 Oct.
- **Phases A–D** need the **test build** (not built yet) and one combined session with the real installation. Every event is logged at high resolution and marked, so the log can be decoded afterwards without manual note-taking.
- **Phase E** is a short keypad/menu bench session, docked, everyday firmware.

**Go-ahead for the test build:** today's start and two normal mowing days look right (open_items.md → Check first), Phase 0 has passed, and the WiFi has been improved.

## Phase 0 — 2 Oct features (everyday firmware, docked)
Do these docked, outside the work period (before 11:00 or after 15:30 on a weekday, or at the weekend), capture log running. Before starting, make sure `timer_normal` in the yaml still matches the timer set on the mower (11:00–15:30, WH2 off, Mon–Fri), because Restore and the self test write it back.

| ID | Test | What to do | Pass |
|---|---|---|---|
| 0.1 | Auto-wrap | Keypad Menu Path = `3231` (24h time, harmless), press Send | Log shows `[rm3231rs]`; Status Byte4 shows `24` during it; screen back on the home screen; mower still parked |
| 0.2 | Park | Press **Automower Park (timer off)** | Timer Control = `parked - auto restore 23:00`; Timer = waiting; Next Start = `none`. If it reads `park NOT confirmed`, Work days all off isn't accepted: change `timer_park` to `m1100000000o r m1200000000o` and re-test, after checking 00:00–00:00 doesn't mean "mow all day" |
| 0.3 | Restore | Press **Automower Restore Timer** | Timer Control = `normal timer (next start <next weekday> 11:00)`; Next Start matches |
| 0.4 | Self test | Press **Automower Keypad Self Test** (~3 min) | Self Test = `PASS … 1 HOME ok, 2 window ok, 3 restore ok, 4 AUTO ok`; Mode back to AUTO; Next Start unchanged |
| 0.5 | Auto restore | Press Park one evening before 23:00 | At 23:00 Timer Control goes back to `normal timer…` by itself; next morning's start happens |
| 0.6 | Park while mowing *(when convenient)* | During a normal mow inside the work period, press Park | Goes home as `03` with AUTO still selected (b3 `00`), docks, Timer waiting. Then Restore Timer. This is the real use case |

Then set up the optional ntfy automations on Timer Control ("FAILED" / "NOT confirmed") and Self Test.

## 1. Test-build firmware structure

Split the config with ESPHome `packages:` so both builds share one code base:

| File | Contents |
|---|---|
| `automower_base.yaml` | Everything in today's `automower.yaml` |
| `automower.yaml` | `packages: base` only — the everyday build |
| `automower-test.yaml` | `packages: base` + the test package below — **same device name**, so it is just flashed over OTA and flashed back afterwards |

After flashing back, the test-only entities show as unavailable in HA; delete them.

## 2. Test-build features

1. **Flight recorder**
   - While active (mowing / leaving / going home / fault / waiting for loop): poll tilt, blade and wheels every **2s**. Poll 0x12 every **5s**.
   - Keep the last ~60s (30 samples) in RAM. Don't publish or log these samples normally.
   - Dump the buffer, plus the next ~20s, to the log on any trigger:
     - 0x12 → `07` (fault) or `08` (waiting for loop signal)
     - lifted or collision flag
     - blade → 0 rpm while mowing
     - state → `03` (going home) or `05` (leaving)
     - any test marker
   - Rate-limit the dump (weak WiFi) and tag it with the trigger.
2. **Test markers**
   - A **Test Step** select (the step IDs below) plus a **Mark** button: logs `=== STEP <id> START ===` with the time.
   - A **Screen Text** text box plus a **Log Screen** button: logs the mower's display message verbatim.
   - A **Log Stats** button: reads and logs 0x18 (especially the error counter, bytes 14–15), the last event record 0x18 `[05 01]`, and the long 0x12 frame. Also runs automatically at each Mark and on every fault.
3. **Fault-clear sequence button**
   - While in a fault, sends 0x0E stop → home → auto → manual → search → start in turn, 10s apart.
   - After each: logs the reply status and the next 0x12 frame. Stops early if `07` clears.
   - Then variants: stop + home; PIN re-login (0x0C) + home.
   - Reminder: start/auto/manual are AND-gated with a keypad Start, so a `00` reply alone proves nothing — watch the 0x12 frames.
4. **Extra buttons:** `[rmh2]` (mode without Start) and **AUX1 Search** (0x0E 05). AUX1 Home and AUX1 Stop already exist; Search is also reachable today via MQTT `automower/cmd` = `search`.
5. **Log level switch** (INFO / DEBUG / VERBOSE) from HA, so every frame can be captured for a short window if needed.

## 3. Preconditions for the session

- Battery full, mower on the base, AUTO, timer covering the session (or set wide for the day — restore it afterwards and keep `timer_normal` in step).
- Test build flashed; capture-log automation running; WiFi as good as it can be.
- Note the station lamp colour before starting.
- **Safety:** main switch to 0 before touching the underside or blade. Keep people and pets off the lawn. Only unplug the station's low-voltage lead or mains, never cut wires.

## 4. Test sequence

Ordered to keep wiring changes together and to use one battery sensibly. For each step: select the step, press **Mark**, do it, then type the screen message and press **Log Screen**.

### Phase A — docked, before going out (~15 min)
| ID | Test | What to do | Answers |
|---|---|---|---|
| A1 | Mode without Start | Press `[rmh2]` (no `s`) | Is Start needed to commit? The manual says yes: expect a beep and it stays parked |
| A2 | AUX1-only Home | With HOME not selected, press AUX1 Home | Does 0x0E home work without the keypad? |
| A3 | L4 – station mains pulled while docked | Unplug station power 2 min, then restore | Distinct from a loop break? `08`? Event record / error counter? Lamp? |
| A4 | Start | `[rmh2s]` | Leaving-station frames at 2s: `05 10` and bit4 timing vs blade spin-up |
| A5 | Remote start | Settings → Remote start = Always, then `[rmh2s]`; set it back afterwards | Outward guide-following frames (`02 02`?): does bit1 = following a wire in either direction, and bit3 = homing only? |

### Phase A2 — static placement, mower stopped (~15 min)
The mower senses which side of the guide wire it is on, and inside/outside the loop. Hypothesis: 0x12 **b1 bits 1–3 encode guide/boundary proximity or side**.
- Seen so far: `0B` following the guide, `0C` docked, `07`/`06` mowing near the boundary just before the wire cut on 30 Sep, `01` normal mowing.
- Method: keep the Hatch **ON** so the mower stays stopped and awake, carry it to each spot, press **Mark**, and wait for two 0x12 polls.

| ID | Position | Answers |
|---|---|---|
| P1 | Open lawn, far from any wire | Baseline b1 (expect `00`) |
| P2 | Just LEFT of the guide wire (facing away from the station) | Side bit? |
| P3 | Just RIGHT of the guide wire | Side bit? |
| P4 | Straddling the guide wire | Both bits? |
| P5 | Just inside the boundary wire | Boundary-near bit (bit2?) |
| P6 | Over the boundary wire | |
| P7 | Just outside the boundary *(if it reads anything at all)* | Inside/outside |

**Z axis (tilt frame bytes 6–7).** Same method: Hatch ON, mower awake, 30s at each position. Measure the angle with a phone. Fast-poll tilt every 2s.

| ID | Position | Expect if Z = gravity axis (1g ≈ 970) |
|---|---|---|
| Z1 | Level | ~970, X/Y ≈ 0 |
| Z2 | Nose up on a block, ~20° (measure it) | 970·cos20 ≈ 910; check which of X/Y reads the 20° |
| Z3 | Side up, ~45°, **left side up then right side up** | ≈ 690; also rechecks the wrong-sign left-roll point |
| Z4 | On its side, 90° | ≈ 0 |
| Z5 | Upside down (blade stopped, hatch open, gloves) | ≈ −970 |
| Z6 | Tip quickly from level to 45°, then hold | Do X/Y lag behind Z? (28 Sep 18:15 showed Z 640 for 40s while X/Y still read level) |
| Z7 | Lift straight up and hold, then put down | Brief change only? Lifted flag? |

### Phase B — out mowing (~45 min)
| ID | Test | What to do | Answers |
|---|---|---|---|
| B1 | Baseline | Let it mow 10 min untouched | Flight-recorder baseline; natural bumps; does the blade stop on its own (as 28 Sep 17:47, 29 Sep 13:17)? |
| B2 | Deliberate bump | Put a solid obstacle in its path a few times | Does the blade stop on a bump? bit4 on restart? |
| B3 | Stop on lawn | Hatch ON (= STOP) 30s, then close without Start | Is `01` "stopped on lawn" distinct from idle? What does closing without Start do? |
| B4 | Restart | `[rmh2s]` | Restart from the lawn |
| B5 | Lift fault + fault-clear | Lift the shell until "Lifted", put it down, then press the Fault-clear sequence button. Then, in separate faults if needed, the controls: keypad OK alone, and a hatch open/close cycle alone | Can any AUX1 command clear a fault? Which of OK / hatch clears it? Lift frames at 2s |
| B6 | L1 – no loop signal | Unplug station mains while mowing; restore after ~6 min | The 30 Sep wire cut gave `08` (waiting, auto-resume), not `07`. Does mains-off give the same? ~5½-min standby wake cycle? |
| B6b | L1b – loop broken mid-mow | Open the loop-wire patch connector while mowing; reconnect after ~6 min | Repeats the 30 Sep event under control. Mains-off vs wire-break: same code and event record? |
| B7 | Restart after L1 | If it hasn't resumed by itself, clear with OK / `[rmh2s]` | Recovery sequence |
| B8 | L3 – boundary crossed *(optional)* | Main switch 0, swap boundary leads at the station, restart | "Outside working area" frames; restore wiring afterwards |
| B9 | Search mode | AUX1 Search (0x0E 05) | Does it work from AUX1 alone? Is search distinct from home in 0x12? |
| B10 | Trapped / no drive *(optional)* | Box it in so it can't escape | "Trapped" / "No drive" frames — stuck-detection data |

### Phase C — return and dock (~20 min)
| ID | Test | What to do | Answers |
|---|---|---|---|
| C1 | L2 – guide wire disconnected | Unplug the guide lead at the station, then Mode Home | Long `03` without bit3? How it hunts. **Limit to ~10 min**, then reconnect |
| C2 | Guide restored | Reconnect the guide lead | Finds the guide → follows → docks. 2s frames of reversing into the charger: bit4? |
| C3 | ECO while charging | Look at the station lamp (flashing green = ECO loop off) | If b1 stays `0C` with the loop off, bit3 = on charger, not guide signal |
| C4 | Stats | Log Stats | Error counter and event record after all faults |

After each loop/guide test, restore the wiring and check the station lamp. On the Husqvarna G3 platform, flashing yellow = guide wire problem, flashing red = station antenna fault.

### Phase D — optional, end of session
| ID | Test | What to do | Answers |
|---|---|---|---|
| D1 | Battery pull | Main switch 0, disconnect battery 1 min, reconnect | Is power loss logged (event record / error counter)? **Caution:** the first-boot wizard may run. Keep AUX1 polling off until it's finished |
| D2 | Wake timing | From standby, open Hatch and watch the screen | Time to screen-ready vs the 10s wake wait (could it be shortened?) |

## Phase E — keypad/menu bench session (everyday firmware, docked)
Short session, outside the work period. Restore the timer at the end (Restore Timer) and check Next Start.

| ID | Test | What to do | Answers |
|---|---|---|---|
| E1 | Work days digit order | Work days all on, then toggle off all but one day, e.g. `m13 8 124567 b` (leaves 3 = Wed if Mon=1) | Next Start should land on Wednesday. Confirms Mon=1…Sun=7 |
| E2 | Commit timing | Enter a distinctive Work hours 1, then press Back instead of OK: `[r m1108101030 b r s]`. Read Next Start (0x12 answers status `03` while a menu is open, so only read after the sequence ends). Then Restore Timer | If Next Start moved to 08:10, the value committed on the last digit; if not, OK is required. Repeat the idea for a selection screen |
| E3 | Remote start options | `m36` then step through with the screen visible | Option order (Never … Always) |
| E4 | Security menu | Navigate `m2` and its sub-items, **read only** — don't change the PIN or level | Shortcut digits for later |
| E5 | Work hours 2 / Reset timer | Navigate `m12` and `m14` | Screen types and how they exit |

## 5. What comes out of it
- Screen message ↔ 0x12/0x14 frames and 0x18 event records for: Lifted, No loop signal, Outside working area, Trapped/No drive, guide lost.
- Whether AUX1 can clear a fault, and whether AUX1 Home and Search work alone.
- The meaning of b1 bits 1–3; whether the blade stops on bumps; whether stopped-on-lawn is distinct from idle.
- Error-counter behaviour; the power-loss log.
- Inputs for the alerts: stuck/lifted, guide not found (long `03`), no loop signal (`08`).

## 6. Not covered (deliberately)
- First-boot wizard / factory reset — too risky to the PIN and settings.
- Tools/EXP menu raw queries — speculative, separate session.
- Further protocol sweeps. The 1 Oct differential sweep is done (results in aux1_protocol.md): the schedule isn't readable over AUX1. **Never include 0x10 or 0x36 in a sweep** — they are actions. Long (LEN ≠ 7) 0x12 replies are already logged by the everyday firmware when they change.
