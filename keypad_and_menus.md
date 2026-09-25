# Keypad behaviour and menu navigation

Scope: how the mower's UI reacts to keypad presses and how to drive it remotely. Wiring is in `hardware_build.md`.

## Mower-side behaviour (confirmed)
- **Presses register on release**, so the gap after release matters more than the press length.
- **Home tap:** cycles the operation mode HOME → AUTO → MAN. It does not change the screen.
- **Home hold 3s:** returns to the idle screen from anywhere (2s is not enough). This is blocked while a value-entry field is waiting, so press OK or Back first.
- **Soft keys:** the three soft keys under the display are relabelled per screen. Left = Back, middle = OK, right = **Next/MENU (one and the same key)**.

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
| Value entry | `1010081030` Work hours 1 = 08:10–10:30 | Literal value | **OK commits** (`o`), Back discards; Home hold is blocked until then |
| Checkbox | `13` Work days, then `8` all on, `6` `7` toggle Sat/Sun off | 1–7 toggle a day (assumed Mon–Sun), 8 = all on | **Back** exits (`b`) |

## Key sequence language (automower.yaml)
Used by the **Keypad Menu Path** text entity + **Send Keypad Menu Path** button, and by the MQTT topic `automower/keys`.

| Char | Action |
|---|---|
| `0`–`9` | Press that digit |
| `m` | Menu / Next (right soft key) |
| `o` | OK (middle soft key) |
| `b` | Back (left soft key) |
| `s` | Start |
| `h` | Home tap (cycles HOME/AUTO/MAN) |
| `r` | Return: hold Home 3s |
| `x` | Hold 7+9 together for 3s (Tools/EXP); **pauses AUX1 polling** |
| `e` | Expert = `rmx` (idle, Menu, 7+9) |
| `w` | Wait one gap, no key |
| space | Ignored |

- **Send Keypad Menu Path only:** if the text starts with a digit, `m` is prepended, so `3231` = `m3231`. MQTT and the single-key buttons don't do this.
- **Examples:**
  - `3231r`: set 24h time, then back to idle.
  - `1010081030o`: set Work hours 1 to 08:10–10:30 and save.
  - `m138b`: Work days all on, then exit.
- **One sequence at a time:** a second request while one is playing (including any single-key button) is rejected and logged, never interleaved.

## Timing (substitutions)
| Setting | Value |
|---|---|
| `key_press_ms` | 250ms per key |
| `key_gap_ms` | 1000ms after each key (screen redraw) |
| `key_hold_ms` | 3000ms for `r` and `x` |

The same timing now applies to single-key buttons as well, so each press ties up the keypad for ~1.25s.

## Home Assistant controls
- **Keys:** Keypad Digit 0–9, Start, Home, Next, Back, OK, Return and Expert Mode are momentary buttons; each plays a one-character sequence.
- **AUX1 Polling** (switch): turns off automatically on Tools/EXP entry. Turn it back on after leaving the menu (7+9 again, or a power cycle). It also blocks raw queries.
- **AUX1 mode:** `Automower AUX1 Stop` and `Automower AUX1 Home` buttons, plus the MQTT topic `automower/cmd` (stop/start/home/manual/auto/search).
- **Automower Start Auto:** sends AUX1 auto, then a keypad Start (the AND-gate combination). **Untested.**
- **Automower Stop Loop Pulse:** opens the stop loop for 2s; wakes the mower from standby.
- **Automower Sync Time:** writes the SNTP time and date to the mower.
- **Automower Raw Query** + **Send Raw Query:** send hex bytes; the reply appears in **Automower Raw Response**.
- The old `Automower Command` select was removed. It replayed "stop" at boot.
- **MQTT:** discovery is off; HA uses the native API. MQTT is kept for the LWT and the two command topics.
