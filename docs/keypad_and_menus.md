# Keypad behaviour and menu navigation

Scope: how the mower's UI reacts to keypad presses and how to drive it remotely. Wiring is in `hardware_build.md`.

## Mower-side behaviour (confirmed)
- **Presses register on release**, so the gap after release matters more than the press length.
- **Home tap:** cycles the operation mode HOME → AUTO → MAN. It does not change the screen.
- **Home hold 3s:** returns to the idle screen from anywhere (2s is not enough). This is blocked while a value-entry field is waiting, so press OK or Back first.
- **Soft keys:** the three soft keys under the display are relabelled per screen. Left = Back, middle = OK, right = **Next/MENU (one and the same key)**.
- **Hatch:** the stop loop (lid microswitch) is emulated by the `Automower Hatch` relay. Opening the hatch wakes the mower from standby; **closing it commits** what is on screen, the same as a person shutting the lid.
- **Keys are ignored while the mower is operating** (preparing to leave, mowing) unless the lid is open. Confirmed 1 Oct: see the bracket rule below.

## Operation mode (confirmed 28 Sep)
Menu, then House, then a digit selects the mode directly, whatever the current mode is. Start, then closing the hatch, commits it.

| Sequence | Mode | 0x12 b3 once selected |
|---|---|---|
| `[mh1s]` | HOME: goes to the station and **stays there** until AUTO or MAN is chosen | `20` |
| `[mh2s]` | AUTO: mows and charges on the timer | `00` |
| `[mh3s]` | MAN: mows until the battery is flat; after a charge it creeps ~20cm out and stops | `10` |

- An earlier manual test (`m`, `1`, House, close hatch, no Start) also sent it home. The `[mhNs]` form is the one used in the firmware.
- The selected mode is read back as `Automower Mode` (0x12 b3 & 0x30).
- With HOME selected the timer never shows "waiting": outside a work period b3 reads `20` (bits 0; `21` inside one, seen 5 Oct), and Next Start reads 0 except for a few seconds after the lid closes — see Timer control.

## Menu tree (1200R manual)
```
Main menu (via Menu/Next): 1 Timer / 2 Security / 3 Settings
1 Timer:    Work hours 1, Work hours 2, Work days, Reset timer
2 Security: Change PIN, Security level (Low/Med/High), New loop signal
3 Settings: ECO mode, Time & Date (Set time, Set date, Time format, Date format),
            Language, Country, Reset user settings, About, Backlight (Auto/ON),
            Remote start (Proportion: Never/Rarely/Medium/Often/Always), Calibrate guide
```
Hidden Tools/EXP menu:
- Hold **7+9** for ~2s from the main menu, or from Settings hold **Park**, then add **4**, then add **3**.
- It hides again after a power cycle.
- Never poll AUX1 while it's open. The firmware pauses polling automatically (see below).

## Digit-shortcut grammar (confirmed on this 1200R)
Press Menu, then one digit per level (main item, sub item, …, option). Only **Settings** and **Timer** have been tested; Security has not.

There are three screen types, and each exits differently:

| Type | Example | Digits | Commit / exit |
|---|---|---|---|
| Selection | `3231` Time format → 24h; `331` Language → English | Last digit picks option N | Selects but stays on the screen; exit with **Home hold 3s** (`r`) |
| Value entry | `11` + `08101030` Work hours 1 = 08:10–10:30 (`12` = Work hours 2) | Literal HHMMHHMM | **OK commits** (`o`), Back discards; Home hold is blocked until then |
| Checkbox | `13` Work days, then `8` all on, `6` `7` toggle Sat/Sun off | 1–7 toggle a day (assumed Mon–Sun), 8 = all on | **Back** exits (`b`) to the Timer menu; follow with `r` |

Other confirmed sequences: Calibrate guide `[m37o]`. Remote start `m36` + option (1 Never … 5 Always) + `r` (from the manual order, unverified).

## Key sequence language (automower.yaml)
Used by the **Keypad Menu Path** text entity + **Send Keypad Menu Path** button, and by the MQTT topic `automower/keys`.

| Char | Action |
|---|---|
| `0`–`9` | Press that digit |
| `m` | Menu / Next (right soft key) |
| `o` | OK (middle soft key) |
| `b` | Back (left soft key) |
| `s` | Start |
| `h` | Home (House) key tap |
| `r` | Return: hold Home 3s |
| `x` | Hold 7+9 together for 3s (Tools/EXP); **pauses AUX1 polling** |
| `e` | Expert = `rmx` (idle, Menu, 7+9) |
| `[` | Open hatch (holds the stop loop open), then a 1.5s settle, plus the wake wait if the mower was asleep |
| `]` | Close hatch (**commits**), then a 1.5s settle |
| `w` | Wait one gap, no key |
| space | Ignored |

- **Send Keypad Menu Path only:** if the text starts with a digit, `m` is prepended, so `3231` = `m3231`. MQTT and the single-key buttons never prepend.
- **Auto-wrap (firmware 2 Oct).** Send Keypad Menu Path and the MQTT topic `automower/keys` wrap what you send as `[r` + text + `rs]`, so you only type the settings part:
  - Typing `1111001530o` sends `[rm1111001530ors]`: lid open, Home hold, Menu (prepended for a leading digit), the settings, Home hold, Start, lid closed.
  - Text that already starts with `[` is played exactly as typed (full control).
  - Text starting with `!` is sent raw without any wrapper (e.g. `!s` = just press Start), for diagnostics.
  - The single-key buttons and the mode buttons are not wrapped.
  - So a "why did the lid open and Start get pressed?" in the log after a menu edit is the wrapper doing its job.
- **Standby guard (firmware 5 Oct).** If the mower drops to standby while the hatch is open mid-sequence, the sequence is replayed once from its `[` (with a wake); if it drops again the sequence is aborted and logged.
- **Rule (1 Oct, updated 2 Oct): wrap every sequence in `[r … r s]`.**
  - The keypad ignores keys while the mower is operating. On 1 Oct a timer restore sent without brackets, while the mower was preparing to leave and then mowing, never registered: 0x12 b4 never showed `24`, and it had to be re-entered by hand.
  - With the lid open it is a normal keypad session in any state (docked, idle, mowing or asleep). `[` also wakes it and waits 10s for the screen.
  - **Start with `r` after the `[`** (2 Oct). The first try of `[m1219151945ors]` at 19:10 was lost: the mower was awake (no wake wait) but about to go to standby, the keypad marker cleared mid-sequence and it slept 15s after. Most likely the screen was dark and the first key only woke it, so `m` was swallowed and the digits landed on the main screen. The retry from full standby (with the 10s wake wait) worked. A leading `r` (Home hold) wakes the screen and guarantees the home screen; it does not change the mode.
  - Inside the brackets: one or more setting groups, each ending with `r` (back to the home screen), then `s`, then `]`. The `s` matters because the manual says Start must be pressed before the lid closes, or the mower won't operate.
  - Docked outside the work period, `s` leaves it parked and waiting. Inside the work period it leaves, which it would have done anyway.
  - End timer edits with `r`, not `b`. `b` only steps back to the Timer menu and leaves a menu open (0x12 status `03` for minutes afterwards).
  - The ESP now adds this wrapper itself (see Auto-wrap above).
- **Examples:**
  - `[rmh2s]`: open hatch, Home hold, select AUTO, Start, close hatch (what the Mode Auto button plays since 2 Oct; `[mh2s]` before).
  - `[r m1219151945o r s]`: Work hours 2 = 19:15–19:45 (2 Oct: Next Start then read Fri 19:15).
  - `[r m1108101030o r s]`: set Work hours 1 to 08:10–10:30, save, Start, close.
  - `[r m13867b r s]`: Work days Mon–Fri.
  - `[r m1111001530o r m1200000000o r m13867b r s]`: full timer restore (11:00–15:30, Work hours 2 off, weekdays).
  - `[r m3231r s]`: set 24h time.
- **One sequence at a time:** a second request while one is playing (including any single-key button) is rejected and logged, never interleaved.

## Timing (substitutions)
| Setting | Value |
|---|---|
| `key_press_ms` | 250ms per key |
| `key_gap_ms` | 1000ms after each key (screen redraw) |
| `key_hold_ms` | 3000ms for `r` and `x` |
| `hatch_settle_ms` | 1500ms after `[` and `]` |
| `wake_wait_ms` | 8500ms extra after `[` if AUX1 was asleep (10s total) |

The same timing applies to single-key buttons, so each press ties up the keypad for ~1.25s.

## Home Assistant controls (firmware 17 Nov)
- **Automower Park-Mow** (switch): **off = park, on = mow.** Plays the `park` / `mow` commands below; the timer is never touched. The setting is kept in flash but not replayed at boot. Result in **Automower Command**.
- **Automower Top-up Now** (button) and **Nightly Top-up** (switch, 02:00): wake the mower (lid open/close, no Start) and report the charge current in **Automower Top-up**; the clock is synced if it is more than 30 s out. Parked (AUTO, waiting for Start), each wake charges it to full. Since 17 Nov it no longer skips "not docked" after an ESP restart (uses the last known docked state).
- **Keypad Menu Path** + **Send Keypad Menu Path**: runs any command text (see "Commands" below), e.g. `[rmh2]>auto_stopped`. MQTT `automower/keys` does the same. The box is cleared once sent, so a second press can't resend it.
- **Automower Hatch** (switch, configuration section): on = hatch held open, off = closed (commits). For testing.
- **AUX1 Polling** (switch, configuration): master on/off for AUX1 serial traffic. Turns off automatically on Tools/EXP entry; turn it back on after leaving that menu.
- **Automower Raw Query** + **Send Raw Query** (diagnostic): hex bytes, reply in **Automower Raw Response**.
- **MQTT:** discovery off and `topic_prefix: null`; HA uses the native API. MQTT carries `automower/status` (LWT), `automower/debug` (log stream, used for capture logging), `automower/keys` (commands) and `automower/cmd` (AUX1 0x0E stop/start/home/manual/auto/search, no button).
- **Removed 17 Nov:** Mode Home / Auto / Man, Park (timer off), Restore Timer, Timer Auto Restore, Skip Today, Keypad Self Test, Sync Time, AUX1 Stop / Home buttons, the 17 single-key buttons, the Timer Control / Skip / Self Test text sensors and the Status Byte3 / Byte4 sensors. The sections below on Skip Today, Timer control and the self test are kept for their findings.

## Commands (firmware 17 Nov)
One routine, `run_command`, plays a command: steps separated by `;`, each `keys>expected`. Keys are in the key sequence language above (wrapped as `[r ... r s]` unless they start with `[` or `!`).
- **Before each step** it reads 0x12. If the mower is already in the expected state, the keys are skipped.
- **After the keys** it checks the state: `docked` waits up to 90 min; the others read 2 s after the lid closes and again 6 s later.
- It stops at the first failed step. Result in **Automower Command** and the log (`[I][cmd]`).
- Commands queue: one sent while another runs (switch, Send, MQTT) waits its turn, up to 3 waiting.

| Expected | Meaning (0x12) |
|---|---|
| `docked` | on the charger: state `04`, or b1 bit3, or `01` when it was last seen docked (`01 00` alone can't tell docked from stopped on the lawn) |
| `auto_stopped` | AUTO selected (b3 & 0x30 = `00`), b2 `05` — waiting for Start |
| `auto_armed` | AUTO selected and b2 `06`, or leaving / mowing |
| `home` / `man` | that mode selected |
| *(none)* | no check |

Commands in the substitutions:
- `cmd_park: "[rmh1s]>docked; [rmh2]>auto_stopped"` — HOME + Start sends it home (skipped if already docked), then AUTO selected and the lid closed **without** Start.
- `cmd_mow: "[rmh2s]>auto_armed"` — AUTO + Start; the timer decides when it goes out.

Notes from the first runs (17 Nov):
- **Any command that opens the lid and closes it without `s` leaves the mower stopped** (b2 `05`), even if it was set to mow. Change settings first and Mow last, or end the settings command with `s`.
- Setting the normal timer without starting it: `[rm1111001530orm1200000000orm13867br]>auto_stopped` (WH1 11:00–15:30, WH2 off, work days all on then Sat/Sun off). Takes about a minute of keys.

A new command is a new string (plus a button or switch that plays it). To bring single keys back for testing, paste into `button:`:
```yaml
  - {platform: template, name: "Keypad Digit 0", on_press: [{script.execute: {id: key_sequence, seq: "0"}}]}
  # ... digits 1-9 the same ...
  - {platform: template, name: "Keypad Start",   on_press: [{script.execute: {id: key_sequence, seq: "s"}}]}
  - {platform: template, name: "Keypad Home",    on_press: [{script.execute: {id: key_sequence, seq: "h"}}]}
  - {platform: template, name: "Keypad Next",    on_press: [{script.execute: {id: key_sequence, seq: "m"}}]}
  - {platform: template, name: "Keypad Back",    on_press: [{script.execute: {id: key_sequence, seq: "b"}}]}
  - {platform: template, name: "Keypad OK",      on_press: [{script.execute: {id: key_sequence, seq: "o"}}]}
  - {platform: template, name: "Keypad Return",  on_press: [{script.execute: {id: key_sequence, seq: "r"}}]}
```
(Or type `!0`, `!s` etc. into Keypad Menu Path: `!` plays the keys raw, unwrapped.)

## Skip Today (firmware 10 Oct)
_Removed from the firmware 17 Nov; kept for the findings._
For "not today" (rain, people on the lawn) without touching the timer. Meant to replace Park / Restore Timer for day-to-day use.

| Step | What the ESP does | Automower Skip shows |
|---|---|---|
| **Automower Skip Today** pressed | Plays `[rmh1s]` (HOME): goes home if out, stays docked if not. Checks mode bits = `20` 2s after the lid closes. Sets `skip_pending` (kept in flash, survives an ESP reset). | `skipping - back to AUTO 23:00` (`tomorrow 23:00` if pressed after 23:00), or `HOME NOT confirmed - check mower` |
| **23:00** (`skip_rearm_hour/minute`), only if a skip is pending | Plays `[rmh2s]` (AUTO), only while docked or in standby. Checks mode bits = `00` (re-reads once after 6s). Outside the work hours AUTO just waits on the charger (and stays awake ~25 min, so it charges). | `off - back to AUTO Sat 23:00, next start Mon 12 Oct 11:00` |
| Not confirmed, or not docked | Retried every 10 min, max 6 | `back to AUTO attempt n not confirmed - retrying`, finally `FAILED - still in HOME, press Mode Auto` |
| Mode Home / Auto / Man pressed while a skip is pending | Skip cancelled, so 23:00 never undoes a deliberate choice | `off` |

- Plain **Mode Home** is unchanged: there is no automatic return to AUTO.
- Pressed after 23:00, AUTO comes back at 23:00 the next day, so the next day is skipped too.
- If a Park restore is also due at 23:00, it runs first and the re-arm waits for it.
- **Untested as of 10 Oct.** First test: press Skip Today while docked → `skipping…` and Automower Mode HOME; at 23:00 → Mode AUTO and a next start in Automower Skip.

## Timer control (firmware 2 Oct) — why the mower's timer may change by itself
_Removed from the firmware 17 Nov; kept for the findings._
**Background.** Clearing the active work window sends the mower straight home in AUTO, and AUTO stays selected (proven 2 Oct 19:17: `03 00 06 02` at once, docked 3 min later). That is cleaner than Mode Home, which leaves HOME selected until AUTO is chosen again. The catch is the timer must be put back afterwards, so the ESP does that.

**What the ESP does, and when:**
| Trigger | What it plays | Result shown in **Automower Timer Control** |
|---|---|---|
| **Automower Park (timer off)** button — ⚠ removes the schedule, see below | `timer_park` = `m138 1234567 b`: Work days all on, then every day toggled off, wrapped `[r … r s]` | `parked - auto restore 23:00` (or `restore manually`). Checked: timer state must read "waiting". With HOME selected: `park sent - HOME selected, can't be verified` |
| **Daily at 23:00** (`timer_restore_hour/minute`), only if parked and **Timer Auto Restore** is on | `timer_normal` = `m1111001530o r m1200000000o r m13867b` (WH1 11:00–15:30, WH2 off, Mon–Fri), wrapped | `normal timer (next start Mon 05 Oct 11:00)`, plus ` - HOME selected` if so. Checked: a next start shown, or in a work period |
| **Automower Restore Timer** button | Same as above, straight away | Same |
| Restore not confirmed (mower didn't answer, keys missed) | Re-read once after 6s, then retried every 10 min, max 6 attempts; the pending flag is kept in flash so it survives an ESP reset | `restore attempt n not confirmed - retrying`, finally `RESTORE FAILED - check the timer on the mower` |

- **Keep `timer_normal` in step with the real timer.** If you change the schedule on the mower, change the substitution too, or the next Restore / self test will put the old one back.
- Park late in the evening (after 23:00) is restored at 23:00 the next day, so it misses a whole day. Turn **Timer Auto Restore** off to stay parked for several days; Restore Timer brings it back.
- Each Park / Restore logs a `timer:` line in automower.log, so a timer change can always be traced.
- **HOME selected hides the timer (9 Oct).** With HOME selected the 0x12 timer bits read `0` outside a work period (never "waiting"), and Next Start shows only for a few seconds after the lid closes, then reads 0. On 9 Oct this made six working 23:00 restores report "not confirmed" and finally "RESTORE FAILED" (Next Start had read Mon 12 Oct 11:00 each time). Since 10 Oct a restore is confirmed by a next start being shown — **verified 10 Oct 22:01**: first attempt reported `normal timer restored, next start Mon 12 Oct 11:00 (HOME selected - won't start until AUTO)`. The standby replay also worked the same evening (Park at 18:59: mower slept at key 5, sequence replayed and completed).
- **Park does not park — answered 7 Nov.** Work days all off is read by the mower as "no restriction" (timer bits `0`, Next Start 0), the same as a cleared timer. After the 6 Nov park, AUTO was selected by hand on 7 Nov and the mower went out every time it was charged, including at 17:58 in the dark. **Don't use Park with AUTO selected; retire it** (or rebuild parking on the 17 Oct no_mow design: AUTO selected, stopped, no Start). The both-windows-off alternative (`m1100000000o r m1200000000o`) would very likely do the same.

## Keypad self test (firmware 2 Oct)
_Removed from the firmware 17 Nov; kept for the findings._
**Automower Keypad Self Test** button, docked only (refused if the mower is out, or the timer is parked). It plays four wrapped sequences and checks each from the long 0x12 frame:
1. `[rmh1s]` → HOME selected, mower docked.
2. Work hours 1 = now −1h … now +1h, all days on → timer "in work period". It stays docked because HOME is selected.
3. `timer_normal` → timer active again, in the same state as before the test.
4. `[rmh2s]` → AUTO selected.

The result goes to **Automower Self Test**, e.g. `PASS Sat 10:05: 1 HOME ok, 2 window ok, 3 restore ok (next Mon 05 Oct 11:00), 4 AUTO ok`. It stops at the first failure. If that happens after the timer was touched (step 2 or 3), it hands the restore to timer control (retried as above). If it fails after step 1, HOME is left selected so the mower can't wander off; press Mode Auto when sorted. Takes about 3 minutes. It proves keys, lid, commit and timer logic, not the exact times (the schedule isn't readable). It overwrites the timer with `timer_normal`, so keep that substitution right.

- 5 Oct run: FAIL at step 2 (no reply) — with HOME selected the mower sleeps ~16s after the lid closes and dropped out mid-sequence. The 5 Oct standby guard (replay once) is meant to fix this; not re-run since. Step 2 ("in work period") does read correctly with HOME selected (b3 `21` on 5 Oct); step 3 compares the timer bits before and after, which HOME hides when outside a work period — re-check that step before relying on the self test.
