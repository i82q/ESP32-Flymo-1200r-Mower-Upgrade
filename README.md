# Flymo 1200R AUX1 bridge

This project gives a 2016–17 Flymo 1200R robotic mower (a Husqvarna G3-platform badge-sibling of the Automower 105/305) WiFi monitoring and remote control. It uses an ESP32 running ESPHome, connected to the mower's unpopulated **AUX1** service port, with relays wired across the keypad matrix.

## What it does
- **Telemetry over AUX1 UART:** state (idle/charging/fault/standby), battery, tilt, collision/lift, blade and wheel motors, and lifetime statistics.
- **Keypad emulation:** all 15 keys through isolated relays. A small sequence language drives the menus (for example, `3231r` sets the clock to 24h and returns to the idle screen).
- **Remote stop and wake:** a relay NC contact in series with the lid stop switch.
- **Always on:** the ESP32 runs from the mower battery, so it survives the mower's standby. It deep-sleeps if the pack runs low.
- **Home Assistant integration:** native API, plus MQTT for availability (LWT) and command topics.

## Layout
| Folder | Contents |
|---|---|
| `firmware/` | `automower.yaml` (ESPHome), `aux1_protocol.h` (protocol helpers), `secrets.yaml.example` |
| `docs/` | `hardware_build.md` (wiring, relay map), `aux1_protocol.md` (decoded protocol), `keypad_and_menus.md` (menus and key sequences), `open_items.md` (status and to-do), `test_build_plan.md` (test plan) |
| `reference/` | Manual, parts list, Husqvarna HRP guide, pinout images, photos |

## Building
1. Copy `firmware/secrets.yaml.example` to `firmware/secrets.yaml` and fill it in, including the mower PIN.
2. Put `automower.yaml`, `aux1_protocol.h` and `secrets.yaml` together in your ESPHome config folder.
3. Compile and flash as usual. `secrets.yaml` and `.esphome/` are git-ignored.

## Cautions
- Never send AUX1 traffic during the mower's first-boot wizard or while the hidden Tools/EXP menu is open. The firmware guards against both; see `docs/aux1_protocol.md`.
- AUX1 start/auto/manual only take effect together with a keypad Start (a safety interlock). Stop works from AUX1 alone.

## Licence
- Code and configuration (the ESPHome yaml and any C++ headers): [MIT](LICENSE).
- Documentation (this README and `docs/`): [CC BY 4.0](LICENSE-docs).

The third-party files in `reference/` (the Flymo user manual and parts list, the Husqvarna HRP startup guide, and the MCP23017 and relay-module images) are **not** covered by these licences. They remain the property of their respective owners and are included for reference only; they will be removed on request. The photos and wiring diagrams are my own and fall under CC BY 4.0.

The AUX1 protocol work builds on the Husqvarna G3 protocol mapping by the mikrocontroller.net community.
