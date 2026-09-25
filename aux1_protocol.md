# AUX1 protocol reference

Scope: the UART protocol, the confirmed decodes and the operating cautions. The implementation lives in `firmware/aux1_protocol.h` / `firmware/automower.yaml`.
This is the same protocol as the Husqvarna G3 platform (Automower 305/310/320/330X, Gardena R40Li), mapped by the mikrocontroller.net community (2013–16).

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
| 03 | Unknown variable / bad request (also appears spuriously while the EXP menu is open) |
| 04 | ? (always an empty payload, probably "not available") |
| 05 | Unauthorised (log in first) |
| 0A | Locked out (5 wrong PINs → 5 min; survives a power cycle) |

## Command map
| Cmd | Function | Auth | State |
|---|---|---|---|
| 0x02 | Time / date / locale config | write only | Decoded |
| 0x04 | Garden settings (corridor, remote start, etc.) | ? | Partly decoded |
| 0x06 | "Timer" | ? | Not the schedule (see below) |
| 0x0C | PIN login | – | Working |
| 0x0E | Mode | yes | Working, but subject to the AND-gate |
| 0x12 | Status | no | Partly decoded |
| 0x14 | Sensors (selector) | yes | Battery, wheels, blade, tilt decoded |
| 0x16 | ? (8-byte payload) | no | Undecoded |
| 0x18 | Statistics | no | Decoded |
| 0x08 0x0A 0x1C 0x24 0x26 0x2A 0x2C 0x2E | ? | always 05 | Not gated by security level |
| 0x00, 0x30 | ? | – | Valid responses, undecoded |

## 0x0C PIN login
- `DATA = 00 01 PIN_lo PIN_hi` (16-bit LE). Example: PIN 1234 = 0x04D2 → `02 0C 04 00 01 D2 04 61 03`.
- This fully unlocks AUX1 with no keypad needed. Firmware re-logs in automatically on a `05` response.

## 0x0E mode
Modes: `00` stop, `01` start, `02` home, `03` manual, `04` auto, `05` search.

**The MMI/AUX AND-gate:**
- Stop is OR'd, so AUX1 alone stops the mower.
- Start, auto and manual are AND'd: they need AUX1 **and** a keypad Start.
- A clean `00` accept does **not** mean the mower will move. Verify via telemetry.
- Security-level writes are gated the same way.

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
Payload `[b0..b5]`:

| Byte | Meaning |
|---|---|
| b0 | State: `01` idle/not charging, `04` on station/charging, `07` fault active. Other values unknown (firmware shows them as `unknown 0xNN`). |
| b1 | Always 00 |
| b2 | Always 05 |
| b3 | Alternates 01/02 with no known cause (not fault-related) |
| b4 | Session/auth-linked (e.g. 0x27 → 0x00 on re-login) |
| b5 | Always 00 |

`LOOP_ON` exists in Husqvarna's own ROS driver (`automower_safe.h`) and probably lives in b0. It is untested because no loop has been connected yet.

## 0x14 sensors — `DATA=[selector, 01]` (tilt also works as `[04]`)
All values are little-endian.

- **01 Battery** (26-byte frame):
  - mV (0–1), mAh remaining (2–3), mA signed (4–5), temperature ×10 (6–7).
  - 8–9 = 1200, probably design capacity.
- **02 Wheels:**
  - R power % (0–1), R speed mm/s (2–3), R unknown (4–5, likely encoder ticks).
  - L power % (6–7), L speed (8–9), L unknown (10–11).
- **03 Blade:** RPM (0–1), current mA (2–3), unknown ~5816 (4–5, not voltage).
  - Free-spin reading is 52–60mA; cutting load is estimated at ~1A.
- **04 Tilt/collision** (17-byte frame): `[collision, lifted, X, Y, unk16, 00 FE 00]`.
  - X and Y are signed at **0.1°/count** (calibrated 20 Sep; right roll positive).
  - One left-roll point came out with the wrong sign and should be rechecked.
  - Bytes 6–7 are a Z-axis or magnitude candidate; unconfirmed.
- **06:** search/exit distance (not decoded further).
- Battery and tilt both reply with CMD 0x15, so **frames are validated by expected length as well as the CMD echo.**

## 0x18 statistics (no auth)
- Total hours (0–1), mow hours (2–3), search hours (4–5), unknown (6–7).
- Charge count (8–9), production date packed (10–13), errors/messages counter (14–15).
- Total hours and charge count are verified against the display.

## Schedule (Timer / Work days) — not readable over AUX1
- **Ruled out:**
  - 0x06 with selectors 00–05, and 0x06 with `[00, index]` (the index is echoed back but the data is identical).
  - 0x04 with selectors 01–05.
  - Security gating.
- **External sources checked:**
  - Robonect: closed firmware, but confirms the timer index and Active/Disabled/Standby states.
  - automowerduino: a different protocol.
  - husqvarnagroup/hcp: the model file isn't public.
- The schedule **can be set** via keypad sequences (`keypad_and_menus.md`). Reading it back remains unsolved.

## Standby / power
- The HRP Startup Guide confirms AUX power is cut when the mower is "passive for XX minutes". The number is not published; measured here at **~20–30 min** outside the work hours.
- A *continuous* driver session prevents standby; periodic polling was not seen to.
- A low battery shuts the mower down regardless.

## Faults
- No AUX1 command clears a fault. Home, auto and search all fail. The fix is the keypad OK button or physical intervention.
- **Known codes:**
  - 51 "Guide 2 not found": a scheduled start with no loop connected; clears when lifted off the base.
  - Lifted = 15 on screen.
  - A `0x22` subcode was seen for lifted in an earlier-format capture.
  - 501–505 are the documented "electronics problem" codes (MMI comms, memory, keypad, display, parameters). 53 is unconfirmed.
- **Transient fault:** a `07` fault that self-cleared within 3s was seen after typing a Work hours start time of "now" with no loop fitted. It is probably from the loop/guide family.

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
