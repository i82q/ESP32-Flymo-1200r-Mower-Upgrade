# Contributing

Findings from other owners are very welcome — this project only exists because the protocol was mapped bit by bit.

## Most wanted
- **Fault codes:** the screen text plus the 0x18 `[05 01]` event record taken right after the fault.
- **Unknown bytes:** see "Protocol unknowns" in `docs/open_items.md` (0x12 b1 bits 1–3, b4 values, long-frame bytes 18–21, 0x16/0x28...).
- **Other models** on the Husqvarna G3 platform (Automower 105/305/310/320/330X, Gardena R40Li): does the AUX1 port, PIN login and decode work the same?
- **Keypad / menu behaviour** listed under "Keypad / menu unknowns".

## How
- **A finding:** open an Issue using the *New finding* form. Logs and frames are the most useful part; remove your PIN first (the 0x0C login frame contains it).
- **A fix or doc change:** fork the repo, edit, and open a pull request. Small focused changes are easiest to review.
- **Questions:** open a normal Issue. Replies may be slow — this is a hobby project.

## Credit and licence
Contributors are credited in the README. By contributing you agree that your code is released under the [MIT licence](LICENSE) and your documentation under [CC BY 4.0](LICENSE-docs), the same as the rest of the project.

## Safety
Never sweep AUX1 commands 0x10 or 0x36 — they are actions, not reads (see `docs/aux1_protocol.md`). Main switch off before touching the underside or blade.
