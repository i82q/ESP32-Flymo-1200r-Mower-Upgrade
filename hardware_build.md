# Hardware build (as-built, current)

Current state only. Code source of truth: `firmware/automower.yaml` + `firmware/aux1_protocol.h`.
Protocol details: `aux1_protocol.md`. Keypad behaviour/menus: `keypad_and_menus.md`.

## Controller
- ESP32-WROOM-32D 38-pin dev board (HW-394, CH340). ESPHome `board: esp32dev`, Arduino framework.
- Powered from the mower's **main battery via a buck converter set to 5V** into the ESP32 5V/VIN pin. The onboard regulator makes 3.3V.
- Why not AUX1 pin 6: AUX1 power is cut when the mower goes into standby. The ESP32 must stay alive through standby to drive the stop-loop wake.
- AUX1 pin 6 is fully disconnected, so there are no competing 5V sources.
- Brownout watch: the `debug` component, `Automower Uptime` and `Automower Reset Reason` are there to catch relay-coil current sagging the 5V rail.

## GPIO map
| GPIO | Use |
|---|---|
| 16 | UART RX ← AUX1 pin 1 (mower TX) |
| 17 | UART TX → AUX1 pin 2 (mower RX) |
| 21 | I2C SDA → MCP23017 |
| 19 | I2C SCL → MCP23017 |
| 34 | Pack voltage ADC (divider, ADC1) |
| 4 | AUX1 awake sense |

## AUX1 connector
- Location: 8-pin Micro-MaTch on the main board (588 62 98-03 Rev G), bottom-left, next to TEST. It was unpopulated from the factory.
- Termination: a proper 8-pin Micro-MaTch plug. The male pins had to be **bent slightly outward** for contact tension. Before that fix, the symptom was total UART silence, sometimes with TX echoing back on RX.

| Pin | Signal | Used |
|---|---|---|
| 1 | UART_TX (115200 8N1) | → GPIO16 |
| 2 | UART_RX | ← GPIO17 |
| 3 | GND | ESP GND |
| 4 | AUX_GPIO (undocumented spare, also blank in the HRP guide) | – |
| 5 | 3.3V (400mA max) | awake sense |
| 6 | 5V (750mA max, 4.9V measured under load) | **disconnected** |
| 7 | ~18V (1A max), not measured | – |
| 8 | GND | – |

## Sensing circuits
- **Pack voltage:** battery+ → 100k → GPIO34 → 15k → GND.
  - The yaml uses `pack_multiplier: 7.57`, but the 18 Sep calibration gave 6.972.
  - ⚠ Confirm by comparing `Pack Voltage` with the AUX1 `Battery Voltage` at the same moment.
  - A 100nF filter cap on the ADC node is optional and not confirmed fitted.
- **AUX1 awake:** AUX1 pin 5 → 10k → GPIO4, with 100k from GPIO4 to GND.
  - Digital only: shows whether the rail is present or absent.
  - GPIO5 was avoided because it is a strapping pin.
- **Low-battery protection:** if the averaged pack voltage drops below 18V, the ESP32 releases all relays and deep-sleeps for 1h, then re-checks.

## I2C expander
- MCP23017 at **0x27** (A0–A2 tied high). Powered from the ESP32 3.3V rail.

## Keypad + stop loop: relays (replaced CD4066 because of leakage)
- **Relay boards:** two 8-channel active-low relay boards (Elegoo-style, optocoupled).
  - Opto side VCC = 3.3V; JD-VCC = 5V for the coils, with the JD-VCC jumper removed.
  - All relay pins are `inverted: true` and `restore_mode: ALWAYS_OFF`; ESPHome "off" = de-energised.
- **Keypad contacts:** each key uses a relay's **NO** contact across its ribbon matrix pin pair. This gives full galvanic isolation, so there is no leakage path.
- **Stop loop:** uses **board 2 relay 8 NC**. De-energised = loop closed. This is fail-safe when the ESP32 is off or deep-sleeping.
- **In Home Assistant:** the relay switches are `internal: true`. HA only sees momentary buttons, so a relay can't be left latched on.
- `reference/keypad_wiring_annotated_v2_4066_era.png` shows the old CD4066 build and no longer matches the wiring.

### Keypad ribbon matrix (10-pin), from `reference/keypad_pinout.png`
Each key shorts one of pins {1–5} to one of pins {6–10}. Column 2 carries 1/4/7, column 3 carries 2/5/8/0 and column 4 carries 3/6/9.

### Relay map (ESPHome numbering: 0–7 = PA0–7, 8–15 = PB0–7)
| MCP pin | Board / relay | Matrix | Key |
|---|---|---|---|
| PA0 (0) | B1 R1 | 2+6 | Back (left soft key) |
| PA1 (1) | B1 R2 | 2+7 | 1 |
| PA2 (2) | B1 R3 | 2+8 | 4 |
| PA3 (3) | B1 R4 | 2+9 | 7 |
| PA4 (4) | B1 R5 | 4+9 | 9 |
| PA5 (5) | B1 R6 | 4+8 | 6 |
| PA6 (6) | B1 R7 | 4+7 | 3 |
| PA7 (7) | B1 R8 | 4+6 | Next / MENU (right soft key) |
| PB7 (15) | B2 R1 | 3+10 | 0 |
| PB6 (14) | B2 R2 | 3+9 | 8 |
| PB5 (13) | B2 R3 | 3+8 | 5 |
| PB4 (12) | B2 R4 | 3+7 | 2 |
| PB3 (11) | B2 R5 | 3+6 | OK (middle soft key) |
| PB2 (10) | B2 R6 | 1+6 | Start |
| PB1 (9) | B2 R7 | 5+6 | Home |
| PB0 (8) | B2 R8 **NC** | stop loop | Stop loop |

## Stop loop (remote stop / wake from standby) — WORKING
- The lid/stop microswitch is **NC with the lid closed**, and **opens** when the stop button is pressed or the lid is opened.
- The **STOP header** on the main board (top-left) and a header on the MMI board are **parallel taps** onto the same node. Shorting them wakes the mower, but the headers are **not** valid series splice points.
- The relay's NC contact is spliced **in series into the microswitch's own two wires**.
- The "Automower Stop Loop Pulse" button energises the relay for 2000ms, which opens the loop.
- **Confirmed: this wakes the mower from real standby.**

## Mower sensors (mechanical notes)
- **Parts:**
  - Sensor board: `588 57 21-01`, a separate PCB on the lower chassis.
  - Loom: `588 90 12-01`.
  - Collision plate: `576 03 01-01` (chassis).
  - Collision magnet: `576 18 33-01` (shell).
- **How it works:** the shell floats on the chassis. Hall sensors detect relative movement for collision and lift, and the same board also senses the boundary loop.
- **Bench behaviour:** "collision active" and "lifted" stuck on during bench tests are expected when the shell isn't seated. This is not a board fault, since the lift sensor responds correctly to a magnet.
