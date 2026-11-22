# Gamepad and TV remote: table of the games that had no mapping

[← back to README](../README.md)

Pad = can the game be played with the gamepad (the keys of the pad come from the defaults and the `pad:` entries of `REMAP`).
CEC = can it be played with the TV remote (cursor keys, OK, Back; no letters or digits). `ano` = the game was played for 15 minutes by
`tools/playtest/ptest.py` with only those keys and it responded all the time (the pad and the remote cannot do more than that), `nie` = it needs the keyboard,
`neurčené` = not decided (see the note). The test shows that the game starts and reacts, not that it is comfortable to play; check by hand
(see [games-check.md](games-check.md)).

| Game | Pad | CEC | Note |
|---|---|---|---|
| AIRRAID | ano | ano | R štart na šípke hore, OK/Space = streľba |
| SPACE-RA | ano | ano | A štart na šípke hore, OK/Space = streľba |
| GAME2048 | ano | ano | S/Enter štart, šípky |
| KVADRO | ano | ano | pohyb Q A O P cez šípky, Enter štart |
| KVADRO.E | ano | ano | ako KVADRO |
| TRAILBLA | ano | ano | Enter štart, šípky |
| SMYCKY | ano | ano | F1 štart (OK), šípky |
| VOTRELCI | ano | nie | po konci hry treba 1 alebo 0 (pad: X a Y) |
| TETRIS | ano | nie | menu cez číslice (pad: X = 2 joystick, Start = 0); po hre žiada meno |
| JETPAC | ano | nie | menu cez číslice (pad: X = 4 joystick, Y = 5 štart) |
| PSSST | ano | nie | ako JETPAC |
| JSWILLY | ano | nie | menu 3 = štart (pad: X) |
| MANIC23C | ano | nie | H štart (pad X), 0 spustí hru (pad Y), skok A alebo šípka hore |
| ATOMIX | ano | nie | menu 1 = štart (pad: Start), R znovu (pad: Y) |
| TVARE | ano | nie | menu 1 klávesnica (X), 0 štart (Start) |
| PLOTTING | ano | nie | menu 1 klávesnica (X), 5 štart (Start) |
| PONTRIS | ano | nie | F1 štart (pad: Start), Space prehodí symboly; Enter by hru ukončil |
| PONTRIS2 | ano | nie | ako PONTRIS |
| MISSION | ano | nie | G štart (pad: Y), potom Enter |
| STRIPPED | ano | nie | G štart (pad: Y), 2 štart hry (pad: X), E/D hore/dole, Space streľba |
| FLAPPY | ano | nie | H štart (pad X), E/Y hore/dole, Space hod; kódy len z klávesnice |
| FLAPPY-V1 | ano | nie | ako FLAPPY |
| VIDEOSTP | ano | nie | F1 zastaví kocky (A); meno do tabuľky z klávesnice |
| SOLITER | ano | neurčené | kurzor šípkami, Space/Enter = klik; z titulky sa na OK nedostal automatický test, treba ručne |
| PEXESO | ano | nie | menu 2 = proti počítaču (pad: X) |
| PEXESO2 | ano | nie | menu 2 = proti počítaču (pad: X) |
| PANGO | ano | nie | G štart (pad: Y), Enter na úvodnej strane, O P E D cez D-pad, pauza RCL (RB) |
| DOOM85 | ano | neurčené | hrá sa ~5 min, potom obraz zamrzne (smrť alebo koniec levelu?), nepodarilo sa mi zistiť, ako pokračovať; pad: X = nôž, Y = pištoľ |
| FARAON | nie | nie | textová hra, príkazy z klávesnice |
| MESTO | nie | nie | textová hra |
| BKP | nie | nie | textová hra |
| PODRAZ | nie | nie | textová hra |
| TAVA | nie | nie | textová hra |
| KON | nie | nie | súradnice ťahu z klávesnice |
| GARDNER | nie | nie | písmená riadkov z klávesnice |
| PEXESO-B | nie | nie | čísla riadkov a stĺpcov z klávesnice |
| REVERZ | nie | nie | ťahy súradnicami z klávesnice |
| ELKA | nie | nie | ťahy zadávané z klávesnice |
| BLACK | nie | nie | odpovede A/N a F-klávesy na ťah; neskúšané |
| SACH | nie | nie | ťahy K1–K8 a písmená |
| GALAXIA | nie | nie | príkazy z klávesnice |
| DIKTATOR | nie | nie | menu a textový vstup, farby bez ColorACE |
| PISQORKY | nie | nie | 2 hráči, zadanie mien |
| TENISS | nie | nie | 2 hráči, každý má vlastné klávesy; 1 pad nestačí |
| HLIPA | nie | nie | po štarte žiada presný čas z klávesnice |
| ROBINSON | nie | nie | uhol výstrelu číslom a Enter |
| LEMMINGS | nie | nie | 12 volieb F1–F12 plus kurzor, na pad príliš veľa |
| TANK | nie | nie | iba klávesnica (potvrdil používateľ) |
| CROSFIRE | ano | nie | pohyb I K J L (D-pad), streľba R D S F (Y hore, A dole, X vľavo, B vpravo); CEC nemá čím strieľať do 4 smerov |
| CROSSI | ano | nie | ako CROSFIRE, G štart (pad: Start) |
| LAVICKA | ano | nie | úvod: „Hrali ste už?" N (pad: X) + Enter, rekord 0 (pad: Y) + Enter; skoky D-pad (F1, F2, F9, F10) |
| REVERZ-MC | nie | nie | ľubovoľná klávesa, úroveň 0–9, začať (a/n), hráč o/x, potom šípky; iba klávesnica (potvrdil používateľ) |
| FFRUITS | ano | ano | šípky + Enter; pad: všetky tlačidlá = Enter (bez redefinície kláves) |
| SABOTER | nie | nie | potrebuje klávesnicu (menu a nastavenie ovládania) |

The rest of the games without a pad or TV icon (about 50) are programs, programming languages, editors and educational demos
(category Programovani, Programy, Naucne): they need the keyboard, `nie` and `nie`.
