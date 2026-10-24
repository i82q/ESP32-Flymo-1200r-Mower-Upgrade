# AUX1 protocol reference

Scope: the UART protocol, the confirmed decodes and the operating cautions. The implementation lives in `aux1_protocol.h` / `automower.yaml`.
This is the same protocol as the Husqvarna G3 platform (Automower 305/310/320/330X, Gardena R40Li), mapped by the mikrocontroller.net community (2013–16).

_Last tidied: 5 Oct 2026 (30 Sep – 2 Oct findings merged into the main sections)._

## Byte convention (used everywhere in these docs)
Frame: `STX 0x02 | CMD | LEN | DATA… | CRC8 | ETX 0x03`

- **Response `DATA[0]` = status byte** (`resp[3]`).
- **"Payload" = everything after the status byte** (`resp[4]` onward).
- "payload byte0" means `resp[4]`.
- 115200 8N1.
- CRC8 is Dallas/1-Wire (poly 0x31 reflected / 0x8C, init 0x00) over CMD..DATA.
- Response CMD = request CMD + 1.
- CMD bit7 = write on config commands.
- **Read frames by LEN, never by scanning for 0x03.** There is no byte stuffing, so 0x03 can appear inside the data.

## Status byte
| Code | Meaning |
|---|---|
| 00 | OK |
| 01 | ? (seen once on cmd 0x00, with data) |
| 02 | Invalid value |
| 03 | Unknown variable / bad request. Also returned to 0x12 **while any menu screen is open** (Timer menu, Tools/EXP) |
| 04 | ? (always an empty payload, probably "not available") |
| 05 | Unauthorised (log in first) |
| 08 | Seen from 0x10 after the 1 Oct incident (below) |
| 0A | Locked out (5 wrong PINs → 5 min; survives a power cycle) |

## Command map
| Cmd | Function | Auth | State |
|---|---|---|---|
| 0x00 | Echoes our own recent request bytes (rolling buffer) | – | Meaningless |
| 0x02 | Time / date / locale config | write only | Decoded |
| 0x04 | Garden settings (corridor, remote start, etc.) | ? | Partly decoded |
| 0x06 | "Timer" | ? | Not the schedule (see below) |
| 0x0C | PIN login | – | Working |
| 0x0E | Mode | yes | Working, but subject to the AND-gate |
| 0x10 | **Action/write — not a read.** `[04 01]` broke the link and logged a mower event (1 Oct) | ? | **Never sweep** |
| 0x12 | Status; `[01]` / `[01 01]` returns the long frame | no | Mostly decoded |
| 0x14 | Sensors (selector) | yes | Battery, wheels, blade, tilt decoded |
| 0x16 | Version / build strings ("1200R", 2017 build dates); selector 04 blank when off the station | no | Partly decoded |
| 0x18 | Statistics; selectors 05/06/0C return timestamped event records | no | Decoded (records partly) |
| 0x28 | 2-byte reply (`00 00` / `01 00 00`) | ? | Undecoded |
| 0x30 | Valid replies; unusual selectors return stale fragments of other replies (buffer bleed) | – | Undecoded |
| 0x32 | Always `05`, even right after login (higher/service PIN?) | service? | – |
| 0x36 | `00` OK to every selector — possibly an action, effect unknown | ? | **Never sweep** |
| 0x1A 0x1E 0x20 0x22 0x38 0x3A | `04` n/a | – | – |
| 0x34 0x3C 0x3E | No reply, or a `CMD 7F` error frame (status 02) | – | – |
| 0x08 0x0A 0x1C 0x24 0x26 0x2A 0x2C 0x2E | ? | always 05 | Not gated by security level |

## 0x0C PIN login
- `DATA = 00 01 PIN_lo PIN_hi` (16-bit LE). Example (not the real PIN): PIN 1234 = 0x04D2 → `02 0C 04 00 01 D2 04 61 03`.
- This fully unlocks AUX1 with no keypad needed. Firmware re-logs in automatically on a `05` response.

## 0x0E mode
Modes: `00` stop, `01` start, `02` home, `03` manual, `04` auto, `05` search.

**The MMI/AUX AND-gate:**
- Stop is OR'd, so AUX1 alone stops the mower.
- Start, auto and manual are AND'd: they need AUX1 **and** a keypad Start.
- A clean `00` accept does **not** mean the mower will move. Verify via telemetry.
- Security-level writes are gated the same way.
- Whether home and search work from AUX1 alone is still to be confirmed (test plan A2, B9).
- In practice mode changes are done on the keypad (`[rmh1s]` / `[rmh2s]` / `[rmh3s]`, see `keypad_and_menus.md`).

## 0x02 config
- **Locale read:** `DATA=[02]`.
  - The UK/English payload is `09 08 2F 00 3A 03 00 00`.
  - b4 = country (UK 0x3A), b5 = locale sub-field, b6 = language (English 0x00).
  - b0–1 are a factory code; b2 is unknown.
- **Date read:** `DATA=[01]` → `YEAR_lo YEAR_hi MONTH DAY FORMAT`.
- **Time write:** `DATA=[80 HH MM SS]`.
- **Date write:** `DATA=[81 Ylo Yhi M D 04 00 00 00]`, where 04 = 24h, d.m.y. The "Sync Time" button uses this.
- **Security level:** `DATA=[04]` → `00 01 1E LVL 00`, where Low 03, Med 07, High 3F.
  - An AUX1 write of a different level is rejected with status 02, so it needs the keypad.
  - The level controls the anti-theft alarm, not AUX1 command permissions.

## 0x12 status (no auth)
Normal reply: payload `[b0..b5]` (LEN 7). Decoded from real mows 28 Sep – 2 Oct.

| Byte | Meaning |
|---|---|
| b0 | State: `01` idle (parked on the base fully charged, **or** stopped on the lawn — not yet distinguishable), `02` mowing, `03` going home (hunting + following guide), `04` charging (drops to `01` once full), `05` leaving station, `07` fault, `08` **waiting for loop signal** (not a latched fault; see below). |
| b1 | Flag bits. bit0 = moving/driving (hypothesis). bit1 = following guide. bit2 = candidate "boundary wire close by" (seen mowing near the boundary, `07`/`06`, and always when docked). bit3 = on the charger rather than guide signal (it stays set while charging with ECO on). **bit4 (`0x10`) = blade (re)starting while driving — confirmed 6/6**: every sighting followed a 0 rpm blade sample mid-mow with full speed by the next poll; also seen leaving the station (`05 10`); not set when the blade stops for going home. All bits clear in a fault. |
| b2 | Mode actually running: `05` stopped / not running, `06` AUTO, `07` MAN, `08` HOME. Keeps the running mode through a fault, and stays `06` on an AUTO low-battery return or a timer-window removal. |
| b3 | Bits 4–5 = mode **selected**: `00` AUTO, `10` MAN, `20` HOME (mask `& 0x30`). Bits 0–1 = **timer state**: `0` no restriction, `1` timer set and inside a work period, `2` timer set and outside it (waiting). |
| b4 | Usually 00. `24` = keypad/menu in use (seen on every accepted key sequence; clears after standby). `46` = parked by the timer (at timer end 29 Sep, and docking with the timer waiting 30 Sep). `1B` once, after the 0x10 incident. `27` → `00` on a re-login. |
| b5 | Always 00 |

**Observed sequences:**

| Event | Frames |
|---|---|
| HOME: hunt → found guide → following → docked | `03 01 08 20`, `03 09 08 20`, `03 0B 08 20`, `04 0C 08 20` |
| AUTO low-battery return | `03 01 06 00`, `03 09 06 00`, `03 0B 06 00`, `04 0C 06 00` |
| MAN start from station | `04 0C 07 10` → `02 01 07 10` |
| AUTO start after lift + STOP | `01 00 05 10` → `02 01 06 00` |
| AUTO start from station, exits along the guide | `04 0C 05 00` → `04 0C 06 00` → `02 02 06 00` → `02 01 06 00` |
| Leaving station (29 Sep 13:13:26) | `01 …` → `05 10 06 01` → `02 …` |
| Fault on a HOME run | `07 00 08 20` |
| Fault "Lifted" after ~30 min AUTO (bumper lifted and stuck) | `02 01 06 00` → `07 00 06 00` |
| Boundary wire cut while mowing (30 Sep) | `02 06 06 01` → `02 12` → `08 00 06 01` |
| Timer window removed while mowing (2 Oct 19:17) | `03 00 06 02` at once → `03 09` / `03 0B` → docked `04 0C 06 02` at 19:20 |
| Timer set on the base, fully charged (29 Sep ~11:18): Timer 1 = 11:00–17:00, then work days Mon/Wed/Fri | b3 `20` → `22` (hours part-entered) → `21` (inside 11–17, all days) → `02` (AUTO, Tuesday excluded: screen "waiting… Wednesday") |

**Notes:**
- **`08` waiting for loop signal (30 Sep, boundary wire cut):** it stayed stopped ~24 min, then went to standby, waking every **~5 min 35 s for ~35 s** to re-check, and **resumed mowing by itself** on the first wake after the wire was reconnected. Whether mains-off at the station gives the same code is test B6.
- **Clearing the active work window sends it home in AUTO** with AUTO kept selected — cleaner than Mode Home, which leaves HOME selected. HOME stays selected after docking until AUTO or MAN is chosen.
- **bit0 of b1 is not `LOOP_ON`.** With ECO off (loop stays energised at the station) it still cleared on docking.
- **The AUTO exit supports the b1 decode.** bit1 (following guide) was set while leaving along the guide wire, then cleared once mowing.
- **The fault type is not in 0x12.** A lifted fault looks the same as any other `07`; use 0x14/04 (lifted flag), the 0x18 event record, or the screen.
- **A collision does not show in 0x12.** It shows only in 0x14/04.
- An occasional status-`03` reply with no payload was seen once while going home (28 Sep 22:53); the firmware ignores it.
- The firmware publishes `Automower State` from b0 (+ b2 for auto/man), `Automower Mode` from b3 & 0x30 and `Automower Timer` from b3 & 0x03. State `08` is not handled yet (shows `unknown 0x08`).

### Long 0x12 frame — `DATA=[01]` or `[01 01]` (LEN 0x1A)
First seen unrequested on 1 Oct 13:39:32 as the mower left for cycle 2; returned on demand with a selector (firmware uses `[01 01]`, confirmed 2 Oct).

| Payload bytes | Meaning |
|---|---|
| 0–5 | Normal status b0–b5 |
| 6–9 | **Next scheduled start** (LE epoch, mower local time). Zero while inside a work period. Confirmed 2 Oct 18:56: `30 83 C3 6A` = Mon 5 Oct 11:00:00, exactly the next start (timer 11:00–15:30 weekdays). Firmware sensor **Automower Next Start** |
| 10 | Flag: `02` = next start set, `00` otherwise |
| 11–14 | Mower clock (LE epoch, local time; agreed with real time to ~2 s) |
| 15–16 | Total hours (e.g. 1287, 1288) |
| 17 | Battery % (likely): 100 docked full, 99 at departure, 98→97 in the first minutes, matching mAh/1200 |
| 18 | `09` off the base, `0B` docked after use, `0F` docked untouched. Not "armed" (stayed `0B` after Start) |
| 19 | 00 |
| 20–21 | Unknown, 204–258. Went 253 → 256 after a Work hours 2 entry and → 258 after its removal: possibly a settings-change counter. Not battery temperature |

- The u16 seen in the first capture (2403 mowing / 2916 docked) is unresolved.
- Next start is not the length of the work period (270 min / 16200 s don't appear).
- During the 1 Oct sweep, next start read Sat 00:13 for `[01]` and Thu 20:45 for `[01 01]` seconds apart — those sweep values are unreliable (odd timer set at the time).
- On 2 Oct the mower skipped its Friday 11:00 start (AUX1 never woke). Cause unknown; see open_items.md.

## 0x14 sensors — `DATA=[selector, 01]` (tilt also works as `[04]`)
All values are little-endian.

- **01 Battery** (26-byte frame):
  - mV (0–1), mAh remaining (2–3), mA signed (4–5), temperature ×10 signed (6–7).
  - 8–9 = 1200, probably design capacity. Not a cap: remaining capacity reached 1,634 mAh while charging. The battery is 18 V / 2.1 Ah per the manual.
  - 28 Sep AUTO mow: left full at 20.5V / ~1,200mAh; drew 400–1,000mA while mowing; headed home at ~18.1V / ~340mAh; charged at ~1,050mA.
- **02 Wheels:**
  - R power % (0–1), R speed mm/s (2–3), R unknown (4–5, likely encoder ticks).
  - L power % (6–7), L speed (8–9), L unknown (10–11).
  - Mowing speed ~290–300mm/s.
- **03 Blade:** RPM (0–1), current mA **signed** (2–3), unknown ~5816 (4–5, not voltage).
  - Idle reads about −12mA (the signed fix went in 28 Sep).
  - Free-spin reading is 52–60mA.
  - 28 Sep mow (511 samples): ~2,900 rpm, steady (p5 2,854 / p95 2,963). Current median 254mA, p5 102, p95 459; ~1.5A just before a stall.
- **04 Tilt/collision** (17-byte frame): `[collision, lifted, X, Y, Z, 00 FE 00]`.
  - X and Y are signed at **0.1°/count** (calibrated 20 Sep; right roll positive).
  - One left-roll point came out with the wrong sign and should be rechecked (test plan Z3).
  - Bytes 6–7 (Z), checked against 3,863 tilt frames (1 Oct). Best fit: the vertical gravity axis with **1g ≈ 970** — level 940–1026 (median 966), and ~950–958 on 9–10° slopes (970·cos ≈ 955).
    - **Not** the shell-lift sensor: Z stayed 966–972 through the whole 28 Sep stuck-shell "Lifted" fault.
    - Unexplained: 28 Sep 18:13:35–18:15:15 local, Z went 912 → 706 → 641 → 665 → 638 for 40s while X/Y still read near level, then −38 at X −70° (the mower being tipped by hand to check the blade). Either X/Y are heavily filtered (lagging) while Z is raw, or something else. Afterwards X read exactly 0.0 (re-zeroed?).
    - Small dips (830–890) appear while mowing (bumps).
    - Static placement test: test plan Phase A2, Z1–Z7.
  - The collision flag was confirmed in the field on 28 Sep: bumps go ON and clear within ~10s.
  - **The lifted flag is held for a whole lifted fault** (28 Sep: ON 17:29:15, 20s before the fault, until it was cleared at 17:39:45). A brief collision + lift for 10s without a fault also occurred.
- **06:** search/exit distance (not decoded further).
- Battery and tilt both reply with CMD 0x15, so **frames are validated by expected length as well as the CMD echo.**

## 0x18 statistics (no auth)
- Total hours (0–1), mow hours (2–3), search hours (4–5), unknown (6–7).
- Charge count (8–9), production date packed (10–13), errors/messages counter (14–15).
- Total hours and charge count are verified against the display.
- **`[05 01]` = last event/fault record (timestamped).** `AF F1 BC 6A 02 00 02 FF 01 12` = **30 Sep 11:25:35**, the exact start of the boundary-wire-cut `08`, so code `02 00 02 FF 01 12` ≈ no loop signal. After the 0x10 incident it read 1 Oct 16:41:12 `06 08 01 FC 01`.
- `[06 01]`: another record (25 Sep 08:50; later the current time). `[0C]`: 19 Sep 23:56 + counters. Meaning TBD.

## Schedule (Timer / Work days) — not readable over AUX1
- **1 Oct differential sweep** (normal timer → odd timer 07:13–19:47 / 03:21–04:05 / Mon+Thu → restored): no reply contained any encoding of the odd times (HHMM, minutes, epoch) or the day mask.
- Also ruled out earlier: 0x06 with selectors 00–05 and `[00, index]`; 0x04 with selectors 01–05; security gating.
- External sources checked: Robonect (closed firmware, but confirms the timer index and Active/Disabled/Standby states); automowerduino (a different protocol); husqvarnagroup/hcp (model file not public).
- The schedule **can be set** via keypad sequences (`keypad_and_menus.md`), and the **next start** is readable from the long 0x12 frame — enough to confirm a timer edit took effect.
- Decision (28 Sep): the mower keeps its own timer; the ESP only parks/restores it and sends it home.

## 1 Oct incidents and lessons
- **0x10 incident:** `0x10 [04 01]` at 16:41:12 local got a malformed reply (`02 00 00 00 02`), an 0.8 s stall, and the mower wrote an event record at that second; the next six 0x10 queries returned status `08`. Afterwards b2 went `05`→`06` (AUTO running) and b4 = `1B`; the mower left at 16:46. The odd timer (in a work period) also explains the departure, so the cause is not proven. **0x10 is a write/action command — never include it in sweeps.**
- **Keys are ignored while the mower is operating.** The timer restore sent without `[ ]` at 16:44–16:48, while it was preparing to leave and then mowing, never registered (b4 never showed `24`); it had to be re-entered by hand. Hence the `[r … r s]` wrapper.
- **Ending a Timer edit with `b` leaves a menu open:** 0x12 answered status `03` for 3+ minutes after `m13823567b`. End timer edits with `r`.

## Standby / power
- The HRP Startup Guide confirms AUX power is cut when the mower is "passive for XX minutes". The number is not published.
- **Measured 28 Sep: ~30 min.** Docked (charging, MAN selected), last keypad input 20:53, AUX1 cut at 21:21:47.
- After a hatch close it can drop to standby ~15s later.
- Periodic polling (0x12 every 30s, tilt every 60s) does **not** prevent standby. A *continuous* driver session did in earlier tests.
- Opening the hatch (stop loop) wakes it; AUX1 power returns within ~1s, but the screen needs ~10s in total before it accepts keys (firmware: 1.5s settle + 8.5s wake wait, measured 1 Oct).
- With a timer set, it wakes itself just before a scheduled start (2 Oct: woke 19:14:58 for a 19:15 window).
- In state `08` it wakes every ~5½ min to re-check the loop.
- A low battery shuts the mower down regardless.

## Faults
- **Unverified and doubted:** that no AUX1 command clears a fault. Earlier attempts with home, auto and search failed; keypad OK or physical intervention cleared it. A proper retest is test plan B5.
- **Known codes:**
  - 51 "Guide 2 not found": a scheduled start with no loop connected; clears when lifted off the base.
  - Lifted = 15 on screen.
  - A `0x22` subcode was seen for lifted in an earlier-format capture.
  - 501–505 are the documented "electronics problem" codes (MMI comms, memory, keypad, display, parameters). 53 is unconfirmed.
- **No loop signal** is state `08`, not `07`, and self-recovers (30 Sep).
- **Transient fault:** a `07` that self-cleared within 3s after typing a Work hours start time of "now" with no loop fitted. Probably from the loop/guide family.
- **28 Sep, HOME run:** fault `07 00 08 20`; screen code not recorded.
- **28 Sep 17:29, AUTO:** "Lifted" on screen. The bumper/shell had lifted and stuck (mechanical, not a real lift). 0x12 = `07 00 06 00 00 00`, with the 0x14/04 lifted flag held throughout.
- **28 Sep 18:15, AUTO:** blade current ~1.5A, then a fault; screen code not recorded.
- From now on, read 0x18 `[05 01]` after every fault to build the code map.

## Cautions (both enforced by the firmware's traffic gate)
- **No AUX1 traffic during the first-boot wizard.** Once it corrupted the PIN-set step.
  - Guard: no traffic for 20s after AUX1 power comes up (timed from the GPIO4 sense, not ESP32 boot, because the ESP32 is battery-fed).
  - There is still no AUX1 signal for "wizard active".
- **No AUX1 traffic while the hidden Tools/EXP menu is open.** It causes spurious status `03` responses and PIN re-prompts.
  - Guard: the `AUX1 Polling` switch turns off automatically when a key sequence enters that menu. Re-enable it after leaving the menu.

## Firmware lessons (all built into aux1_protocol.h)
1. Read frames by LEN, not by searching for ETX.
2. Validate the response CMD = request + 1, with one retry.
3. Validate the expected frame length per 0x14 selector.
4. Check the status byte *before* discarding a length mismatch. A short `05` response is the auto-relogin trigger.
5. Text entities must never send on `on_value`. Dashboard re-renders re-fire them, so sending is button-triggered only.
6. Template selects replay their initial option at boot, so don't use a select to send commands.
7. Decode signed fields as signed (`le16s`). Blade current and battery temperature were both read unsigned until 28 Sep.
8. Publish on change only (`aux1::pub`), and log poll replies only when they change. Use `get_raw_state()`, not `.raw_state` (removed in ESPHome 2026.10).
