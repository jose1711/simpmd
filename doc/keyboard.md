# Keyboard

[← back to README](../README.md)

PC keys are mapped by SDL key code (the meaning of the key, not its position). The table is `oKeyMap` in
`simpmd/src/sim_keyboard.c`.

- Letters, digits, [Space], [Enter], `,` `.` `/` and `;` are the same keys on the PMD 85.
- A key stays pressed for at least 60 ms (`PMD_KBD_HOLD_MS`), otherwise a slow machine could miss a short tap.
- [Esc] is not a PMD 85 key: the menus use it, in a game held for `PMD_ESC_MS` (800 ms) it returns to the menu.
- `REMAP` in `games.lst` changes the keys for one game, the gamepad and the web remote control send these same keys
  (the buttons of the web are set with `PAD`), see [games.md](games.md).

## Other keys

| PC | PMD 85 |
|---|---|
| [Y] | Z (the PMD 85 has QWERTZ, so [Z] is on the PC [Y] and the PC [Z] is Y) |
| [F1]-[F12] | K0-K11 |
| [Insert], [Delete], Numpad [+] | WRK, CD, RCL |
| [Home], [Up], [PageUp] | INS, DEL, CLR |
| [Left] or [Backspace], [Right], Numpad [5] | LEFT, RIGHT, HOME |
| [Down] | END |
| [End], [PageDown] | LLEFT, RRIGHT (fast left and right, 3 steps) |
| `-`, `=` | underscore, curly brace |
| `[`, `]` | at sign, backslash |
| `'`, `` ` `` | colon, brace |
| [Shift] (either) | SHIFT |
| [Ctrl] (either) | STOP |

## Reference: which PC key gives which character

What to press on the PC to get a character on the PMD 85-2 (US layout, checked in the emulator by typing every
key without and with [Shift]). The key caps are those of a PMD 85-2, see
[a photo of the keyboard](https://cdn.aukro.cz/images/sk1758901075719/pocitac-tesla-pmd-85-2-so-zasuvnym-modulom-basic-g-85-2-neskusane-241607459.jpeg)
(a sales listing that may disappear). On the PMD 85 the symbol written top left on a key is the one with [Shift].

| PC key | PMD 85 key | without [Shift] | with [Shift] |
|---|---|---|---|
| `1` ... `9` | `1` ... `9` | `1` ... `9` | `!` `"` `#` `$` `%` `&` `'` `(` `)` |
| `0` | `0` | `0` | **`-`** (the key shows an overline) |
| `-` | `= _` | `_` | `=` |
| `[` | `` ` @ `` | `@` | `` ` `` |
| `]` | `^ \` | `\` | `^` |
| `;` | `+ ;` | `;` | `+` |
| `'` | `* :` | `:` | `*` |
| `` ` `` | `[ ]` | `[` | `]` |
| `,` `.` `/` | `< ,` `> .` `? /` | `,` `.` `/` | `<` `>` `?` |
| `Y` | `Z` | `z` | `Z` |
| `Z` | `Y` | `y` | `Y` |
| other letters, [Space] | same | letters | capitals |
| [Left] or [Backspace], Numpad [5], [Right] | `<-`, `HOME` (arrow up left), `->` | | |
| [Home], [Up], [PageUp] | `PTL INS`, `DEL`, `CLR` | | |
| [End], [Down], [PageDown] | `\|<-`, `END`, `->\|` | | |
| [Insert], [Delete], Numpad [+] | `WRK`, `C-D`, `RCL` | | |
| [F1] ... [F12] | `K0` ... `K11` | | |
| [Enter] | `EOL` | | |
| [Shift] (either) | red up arrow key = Shift | | |
| [Ctrl] (either) | `STOP` | | |

Limits:

- The key `{ }` of the PMD 85 is the PC key with the code `+` (SDL `SDLK_PLUS`), which a US keyboard does not have
  ([Shift]+[=] is still the `=` key), so it can not be typed there; its characters were not checked.
- [RST] and the second red arrow key (right of `/`) are not mapped.
- The keys are matched by the key code; a keyboard layout that puts the digits behind [Shift] (Slovak, Czech)
  changes the table.

## What the PMD 85 keys do

Only what is documented; the keys are also in the manuals of the PMD 85, not in this repository.

- **[STOP]** (Ctrl here) interrupts a running tape operation, e.g. recording to tape (not on the PMD 85-1).
- **[Shift]+[RCL]** switches from BASIC to the monitor (PMD 85-2, 2A, 3).
- **[Shift]+[DEL]** starts a program loaded at 0000h (PMD 85-1).
  GPMD85emu uses [Shift]+[PageUp] and [Shift]+[Delete] for these two, here they are [Shift]+Numpad [+] and
  [Shift]+[Delete].
- **[K0]-[K11]** are function keys without a fixed meaning, the program decides (e.g. [F1] is a menu key in MANIC,
  [RCL] and [F12] are buttons of SASKO, DOMCEKY and ROBOTIK).
- **[INS], [DEL], [CLR], [HOME], [END]** edit the line and the screen as the names say.
- **[LLEFT], [RRIGHT]** move the cursor by more than one step (games use them as fast arrows).
- **Under CP/M** the keys produce control codes: [WRK] = Ctrl-K, [RCL] = Ctrl-W, [END] = Ctrl-C,
  [Shift]+[CLR] = Ctrl-A, [K0]-[K11] = Ctrl-A to Ctrl-L (full table: `Ctrl-CPM-PMD85.txt` of GPMD85emu).
- The meaning of **[WRK]** and **[CD]** was not found in the sources available to the author; please add it if you know it.
