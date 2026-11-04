# Changelog

Changes since the original SDL 1 version (tag `sdl1-final`, the last upstream commit `f6c7da3`). Newest first.
The version numbers are not used; the dates are the dates of the work in the
[pmd85console](https://github.com/jose1711/pmd85console) repository the code comes from.

## 2026-11-04

- Tape: a second save replaces the saved tape (it was appended to the first one), the saved tape is flushed after each byte,
  and a new game does not inherit the saved tape of the previous one (its "read the saved tape" flags and its open output file).
- Web remote: the state for the browser is built by the main thread and only copied by the web thread (a data race on the
  game list state, found with ThreadSanitizer).
- Games list: every game is in a category now (the main list has 12 rows instead of about 85). New categories `Plosinovky`,
  `Arkady`, `Strielacky`, `Logicke hry`, the families moved into them (`Plosinovky/MANIC`, ...), `Programy` split into `Grafika`,
  `Texty`, `Databaza`, `Hudba`; MICRHYTM is a program (`Programy/Hudba`), ON-DUCH4 is in the family `Plosinovky/PAMPUCH`.

## 2026-11-03

- New word `TAPEAUTOREWIND` in the note: the tape of the game starts again when it is read to its end (EVIDENCE: a wrong file name can be corrected without a restart).
- DAT-PMD: Save and Load of the data base on the virtual tape (`TAPESAVE`, new `TAPESIGNAL`: the tape signal on the status bit 7).
- Games for the youngest (`Pro nejmensi`): DOMCEKY, POSTARIK, ROBOTIK, SASKO, ZMRZLINY use the model 2 (their texts are in
  lower case, which the model 1 can not draw), the arrows are F1-F4 (K0-K3), Enter / F5 confirm, F12 is the menu, F6 is K5.
- Gamepad keys per game: `pad:<input>=<key>` in `REMAP` (`doc/games.md`); new inputs `lt`, `rt` (triggers), `ls`, `rs` (stick clicks).

## 2026-11-02

- New category `Naucne` (educational programs): BUBLSORT, GONIO, LISSAJ, MESIAC, REZ, ELEKTRO, TESTLOG.
- DOOM85 final: a raycaster for the PMD 85-1 in 8080 assembly (random maze, monsters, knife and pistol, ammo, status
  bar with a face, minimap, E/Y forward/back, [Stop] returns to the title). The tape uses generic faces.
- `make install-data`, `bin/` runs after `make install-data PMD_SHARE=bin/`; README, INSTALL and `doc/` rewritten.

## 2026-11-01

- PMD 85-2A: the ROM at `8000h-8FFFh` is switched off by bit PC4 of the system 8255, writes go to the RAM under the ROM;
  the saved state holds the flags and the 4 kB. Colors of the 2A are not done.
- New tapes: BKP, SPINDIZZY, HIGHENC, KUBANOID, DIKTATOR, JERRY, FRED-4 (family FRED), GALAXIA, MTRON, MYTH families.
- Controls (`KEYS`) for the last ten games that had none (KANKAN, KAREL, KURA, MUR, PUZZLE, PAMPUCH, PENETR, SABOTER, …).
- Menu: families of versions (`#FAMILY`, `MANIC ▶`) and nested categories (`Parent/Name`); alphabetical order;
  cache of the text textures, logos and font (the menu was slow on a Raspberry Pi A+); credits line on the start screen.
- BASIC programs read the keys held for 170 ms (they poll `INKEY` once per loop); `PMD_KBD_HOLD_MS` overrides.
- Fix: the `SHIFT` token of the autoload (DIZZY did not start).
- New debug variables `PMD_PAINTSTATS`, `PMD_PAINTTRACE`, `PMD_PADDLETRACE`, `PMD_SCANTRACE`.

## 2026-10-30 – 2026-10-31

- ROM module (8255 at ports `88h-8Bh`) and BASIC-G; 35 BASIC programs from the `basic` package, LOGO (ZOFKA, MRAVEC),
  KAREL2D; the `FONT` patch with the Czech and Slovak letters of the PMD 85-3.
- Sub categories of the games list, icon of a saved state, wrapped hints.
- `NOTE` token `POKE addr value` (CROSSI, CROSFIRE), `TAPESAVE`, `SHIFT`.
- Fix: TxRDY without an output tape (HLIPA and JASON froze); status reports TxEMPTY too.
- Fix: saved state with Ctrl as STOP; Ctrl+F5 / Ctrl+F9 next to Alt.
- Games from the RM-TEAM packages: COBRA, CROSFIRE, MICRHYTM, MISSION, PANGO, RESCUER, STRIPPED, EVIDENCE, EVIDEDIT, …
- Controls, gamepad and TV remote mapping of many games (INVADERS, BOULDER, FLAPPY, FRED, GLUTTON, HLIPA, HORAC, JASON, …).

## 2026-10-25 – 2026-10-29

- Data paths by `PMD_SHARE`; the games list `games.lst` (name, model, note, remap, pad, keys, input).
- Gamepad support (SDL game controller), `REMAP` with `@Key` and `Short|Long|Held`.
- Remote control over HTTP (`PMD_WEB`, `PMD_WEB_TOKEN`) with a page for a phone.
- HDMI-CEC remote control (key mapping for the Raspberry Pi service).
- Sound: gate 1/4 kHz tones, DC blocker, volume, `PMD_SNDLEVEL`, buffer lead `PMD_SNDLEAD`.
- Model PMD 85-2 in the menu; CPU timing of the PMD 85 (`PMD_SLOWCPU`), `PMD_BEEPSLOW`.
- Experimental saved state (Alt+F5 / Alt+F9).

## 2019 – 2020

- Port to SDL2 and SDL2_ttf, new menu with the logo and the game icons, boot logo, fixes of the Flappy ROM.
