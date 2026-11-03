# Games and the games list

[← back to README](../README.md)

## The list

The menu shows four games with logos. [Up] or [E] (gamepad up) opens the list of all games from `games.lst`
in the share directory. `simpmd/data/games.lst` lists the tapes of `simpmd/data/tapes` that work, according to the
table in `simpmd/README`.

| Key | In the list |
|---|---|
| [Up] / [Down] or [E] / [Y] | select |
| [Left] / [Right] | move by a page |
| [Enter] | start |
| [Esc] (hold it) | back to the menu |

After the start the emulator types the commands itself: `MGLD 00` and, for `MGLD 00, JUMP 2000`, the `JUMP` once
the tape stops loading, so selecting a game is enough. `PMD_AUTOLOAD=0` turns it off (type the commands by hand).

`POKE <hex address> <hex value>` in the note writes a byte to the memory once the tape has stopped loading; put it
after `JUMP`, because a game clears its variables when it starts. It is for a game that reads a variable it never sets
(the RAM of the real PMD 85 is not cleared at power on, here it is zero): CROSSI (Kamikadze) hangs in its explosion
routine when `39A4` is 0, so its note is `MGLD 00, JUMP 0000, POKE 39A4 01`.

`SHIFT` in the note holds [Shift] from the first key of the autoload until the game has started. Magicland Dizzy asks for
it at the start of the load and then runs in the monochrome mode (no blink and gray bits in the video memory); pressing
it later does not help, the load takes 15 s with it.

`NOBLINK` in the note ignores the blink bit of the video memory (bit 7) in the picture and in the web remote. Magicland
Dizzy (for the PMD 85-2A/3) sets the bit on large areas, probably as a color that this emulator cannot show, and the
picture would flash.

`TAPESAVE` in the note keeps what the program writes to the tape in `<share>/saves/<tape name>` (a new save replaces
the file; when built with `PMD_STATEHOME`, in `$HOME/<PMD_STATEHOME>/saves/<tape name>` of the user, see [INSTALL](../INSTALL)) and reads it back after the tape of the game: when the program asks for tape data again and the tape of the
game is at its end, the saved file is read from its start, like a rewound cassette. BASIC-G `SAVE n` and `LOAD n` work
this way (the program stays in the file between runs); it is meant for programs that save (EVIDENCE, editors). Without
the word the bytes written to the tape are dropped, as before. `TAPESAVE <name>` uses `saves/<name>` instead of the
tape name, so that two games share one tape: EVIDEDIT writes the data files that EVIDENCE reads.

`TAPESIGNAL` in the note puts the signal of the tape on the bit 7 of the tape status register (DSR): a tone made in time
from the bytes of the input tape (a 1 is a fast tone, a 0 a slow one, 2400 Bd) while the program looks at the bit; the
tape runs while it measures it, so the bytes of the leader pass by. The bytes are still read from the USART. It is for a
program that measures the tape speed itself with the monitor routine 0x8890 before it reads (DAT-PMD): the routine, empty
in the patched ROM, is back (in the free RAM at 0x9200) and the ROM routine 0x848D returns properly. With `TAPESAVE`
DAT-PMD saves and loads its data base (Save, Load; the name of the file must be the same). The saved tape is read once to its end (no endless rewinding: a Load of a missing name
finds the end of the tape, [Stop] = Ctrl ends it); the next key of the user starts it from the beginning. `PMD_TAPETRACE=1` prints the
bytes the program reads from the tape.

`TAPELOOP` in the note rewinds the tape of the game when the program reads past its end (the cassette is read from the
start again). It is for programs that load data by name from their own tape: LEMMINGS (sectors) and TVARE (levels).

EVIDENCE (Tomas Svec) asks for the maximal address (Enter), the date and the name of the file to load from the tape.
The name has 8 characters: `ZAJ.CIN` and a space, `CLEN.ZAK`, `PROGRAMY`, ` SEZNAM ` (a space on both sides). [F11]
switches to capital letters (it also types a stray character: press it before the name and clear the field). The tape
of the game has the program, EVIDEDIT and the four example data files; a file saved by EVIDENCE (the menu `M`, `S`,
`C`) goes to `saves/EVIDENCE` and is found there when it is not on the tape of the game.

To add a game, copy its tape to the share directory and add a line to `games.lst`.

### BASIC games and the ROM module

The BASIC games (no category, they show `BASIC-G` in the list) run in BASIC-G V2.0, which the PMD 85 has in a ROM module. The emulator has the
module: an 8255 at the ports `0x88`-`0x8B` (the address goes to ports B and C, the byte comes from port A), a 32 KB ROM
pack. The note of `games.lst` describes the game:

```
ROM basic2.rmm, TYPE LOAD 53, TYPE RUN
```

- `ROM <file> [<file>...]`: the files of the share directory go into the ROM pack one after another, each on a 1 KB
  boundary (as the ROM packages of the PMD 85 emulator do). Only `basic2.rmm` is needed here (BASIC-G V2.0, 9 KB).
  With the module the monitor of the PMD 85-2 starts BASIC-G by itself, so there is no `BASIC` command to type.
- `TYPE <text>`: the text is typed with [Enter] when the program scans the keyboard (after the tape stopped loading,
  or after 2 s when nothing was read yet). The usual letters, digits, `"` `(` `)` `=` `+` `-` `*` `:` `?` work.
  Note: the PMD 85 is QWERTZ, the typed `Y` and `Z` are its keys of those names.
- `KEYHOLD <ms>`: the minimal time a key stays pressed for this game (default 60). A BASIC game reads a key once in a pass of
  its loop (`INKEY`), a tap shorter than the pass is missed in a part of the passes (BOMBARDE: about 150 ms, `KEYHOLD 170`),
  a longer hold than the pass moves a figure more than once (VOTRELCI moves the gun by 2 positions with 170 ms, 60 is right).
- `FONT`: the Czech and Slovak letters of the PMD 85-3 for a program of that machine (REVERZ, VOTRELCI, ...). Its texts
  use the character set of the PMD 85-3 (codes `0xC0`-`0xDF` the small letters, the capitals 0x20 higher), the font of
  the monitor ends at `0x7F` and draws dots. The emulator makes the glyphs of the letters á č ď í ľ ň ô š ť ú é ý ž
  and the capitals from the letter of the monitor font and a mark, puts them to `0x9000`-`0x91FF` and sets the table of
  the glyph blocks (`0xC0B0`, copied from `0x8270`). The codes of ä ĺ ó ŕ ř ě ů are not known (no program of the package
  uses them in a word), they stay dots. All BASIC games and BASIC-G have `FONT`.
- `LOAD <n>` takes the number of the block in the tape header, **in decimal** (the monitor `MGLD` takes it in
  hexadecimal). Every BASIC game is its own tape file, loading takes the size of the file / 218 bytes per second.

The tapes come from the packages of pmd85.borik.net (basic.zip). Their programs start with a 2 byte length that
BASIC-G V2.0 does not expect at the beginning of the program (the listing is garbage), so the length word is cut off
and the length and the checksums of the header and the data are written again. The tapes of PMD 85-3 only
(NALET, SLALOM, TEST3A, KLAV-PMD) and the test programs are not included. `PEXESO-B` is the BASIC game, `PEXESO` the
machine code one.

### Tapes of the RM-TEAM packages

The `.ptp` files of the packages of pmd85.borik.net (basic, logo, karel, games_other) have a 2 byte length (`length + 2`)
before every block of data and in front of the body of an autorun program: `lead, header (15), [length][data][checksum]`.
The monitor of the emulator takes that length for data and shifts the program by 2 bytes. The tapes here have it cut off
(the header and the checksum of the block stay, it is valid for the layout `[length][data][checksum]`). The programs
that are loaded by a block number `MGLD nn` work with `00` or with a number below `0x10`; with a bigger number the
monitor stops after the name (`16/? NAME`), so these tapes have the number 0 in the header and are loaded with
`MGLD 00`. GRDEMO of `games_other` does not load (stops after the name) and is not included.

### BKP (Bratrstvo kočičí pracky)

Czech text adventure of 2023 (Petr K. and Libor L.A., after the comic Rychlé šípy), `BKP.zip` of
[pmd85.cz](https://www.pmd85.cz/wp-content/uploads/BKP.zip), PMD 85-1, `MGLD 0E`. The `.ptp` has the 2 byte lengths
(`length + 2`) at the block boundaries, the program has its own loader (it reads the tape bytes by the monitor routine
`0x8DDB` of the PMD 85-1: a picture of the screen `K...`, then the program `P...` of 0x764C bytes to `0x0000`) and would
stop at them with the monitor. The tape here has the three frames cut off: `73 01` before the data of the first block,
`51 16` after its checksum and `5E 76` after the checksum of the picture. The first two bytes of the `.ptp` (`3F 00`)
are cut off too. The loader of the program does not work with the monitor of the PMD 85-2 (`0x8DDB` is in the middle
of an instruction there). Keys: `B` colours (Mono, RGBM, Video, ACE), [Enter] starts, the commands are typed.

### The other game packages of pmd85.borik.net (VBG, 4004/482, Libor Lasota, Lemmings)

The packages `Games VBG.zip` (did=8), `Games 4004-482.zip` (did=9), `Games Libor Lasota.zip` (did=82) and `Game Lemmings.zip`
(did=86) of <https://pmd85.borik.net/wiki/Download> hold, by their block names, almost only games that are in the list
already (the tapes of those packages were added earlier or come from the RM-TEAM and `games_other` packages). New from
them: `JERRY` (VBG software, the block at `0x1100`, started with `JUMP 1100`, works in all models) and `FRED-4` (the version +4
of FRED, "for both types of the PMD", the repo had +3; they are in the family `FRED`). Not added: `IHLAVICK` (a text data
block of the package 4004/482), `LEMMINGS` for 256 kB of RAM (`game-lemmings-256k.ptp`, the RAM expansion is not emulated),
the images of the PMD 32 disks and of the ROM MEGAmodul.

### Games of Libor L.A. (pmd85.cz): MINY and VLAK

Programs of the page <https://www.pmd85.cz/?page_id=236> (Libor L.A.). `MINY` (a minesweeper, 2021, `miny.zip`) and `VLAK` (a
snake with a train, the DOS game of Miroslav Němeček 1993, port 2025, `vlak.zip`) are autorun tapes with their own loader
like BKP (the two or three 2 byte frames of the blocks are cut off, `tools/playtest/unframe2.py` does it for any such
`.ptp`: `[lead][header][frame][data][checksum]` and then `([frame][block of that many bytes])*`, applied to BKP it gives
the tape of this repo byte for byte). Both are `MODEL` `1`: the loader calls the tape routine of the monitor of the
PMD 85-1 (`0x8DDB`) or, when `0x8B6C` holds `0xC5` (the original monitor of the PMD 85-2), that routine, and the patched
monitor of this emulator has other code there, so in the models 2 and 2A they stop in the monitor. The header number of VLAK
(`0x10`) is set to 0 (the monitor refuses numbers above `0x0F`), `MGLD 00`; MINY has `0x0C`, `MGLD 0C`.

- MINY: the menu `L` level (9x9, 15x15, 21x16), `C` colours, `M` the mouse 602 (not emulated), [Enter] starts, `I` info.
  In the game: Left, Right and Down are the arrows, up is the key `End` of the PC (the PMD 85 key LINE LEFT), the left
  button (opens) is the arrow Up (the PMD 85 key DEL), the right button (flag) is `CLR` (PageUp), `K1` restarts, STOP leaves.
  The list maps the gamepad: D-pad, A = open (Space), X = flag (H), Y = restart (F2).
- VLAK: any key starts, QAOP drives the train, `K4` (F4) is the password, STOP goes to the title, on the title `J` the
  joystick, `Z` the sound, `C` colours.

Not added from that page: Tenebra Extended (a licensed port, only from the author's page itch.io), Bad Apple (a 512 kB
ROM MEGAmodul, not emulated), Prince of Persia (a project without a file), Falcon Monitor (a tool for the PMD 85-2A),
TINY BASIC 1.0 (an interpreter, `TINYBASIC.zip`), the programs for the SAPI-1 (another computer).

### TINYBASIC

TINY BASIC 1.0 (Li-Chen Wang 1976) ported by Libor L.A. (2018, <https://www.pmd85.cz/?page_id=716>, `TINYBASIC.zip`), integers
only, the commands `LIST`, `LOAD n`, `NEW`, `RUN`, `SAVE`, `CLS`. The `.ptp` has 13 blocks: `TBASICV1` (the interpreter 1.01 for the
PMD 85-1, `MGLD 01`), `TBASICV2` (1.02 for the PMD 85-2 and higher, `MGLD 02`) and 11 sample programs (type `B`, blocks 3
to 13: SYNTAX, DIRECT, PRINT, FOR_NEXT, IF, GOSUB, ARR_SIZE, INPUT, RND, BOOLEAN, ERASTOTE) that the interpreter loads
with `LOAD n` from the tape that follows it. The repo has two tapes, `TINYBASIC` (model 1) and `TINYBASIC-2` (model 2), each of
the interpreter and the 11 samples, the 2 byte frames of the blocks cut off (the interpreter is an autorun block that has
to be started with `JUMP 0000`), in the family `Programovani/TINYBASIC`.

### MPY85

A subset of Python written for this repo in 8080 assembly (`mpy85/mpy.asm`, 4.6 kB), an interactive REPL for the PMD 85-1 (monitor M1,
the tape `MPY85`, `MGLD 00`, `JUMP 0100`, category `Programovani`). 16 bit integers, strings, `None`, `True`/`False`; `+ - * // / %`
(`/` is the integer division, `//` and `%` floor like Python), comparisons, `and or not`, `if/elif/else`, `while`,
`for x in range(...)`, `break`, `continue`, `def` with `return` and recursion, `print len str int chr ord abs input peek poke`.
The font of the PMD 85 has no lower case, so the input is converted to capitals (names and keywords ignore the case).
Enter confirms, Backspace deletes, the cursor up and down browse the history of the last lines, a block (a line ending with `:`) ends with an empty line. On the PC keyboard the PMD 85 symbols are on
Shift: `(` `)` = Shift+8, Shift+9, `-` = Shift+0, `+` = Shift+`;`, `*` = Shift+`'`, `=` = Shift+`_`. It is a tree walker over the source
text, so it is slow (`fib(15)` is about 7 million instructions). Details, memory map and tests: [mpy85/README.md](https://github.com/jose1711/pmd85console/blob/pi-deploy/mpy85/README.md).

### DOOM85

A raycaster (a first person view like Wolfenstein 3D) written for this repo in 8080 assembly (`doom85/`, 1.8 kB), the tape `DOOM85`, `MGLD 00`, `JUMP 0100`
for the PMD 85-1. 48 columns by 128 rows, a 16x16 maze generated on the PMD 85 at the start of every game (the seed comes from the waiting on the title screen), four wall types, shading by distance, collisions. Arrow left and right turn, E walks forward and Y back, the comma and the period strafe left and right (the PMD 85 convention, the arrows of the PC and the pad are remapped to E and Y)
(the pad: D-pad). A face in the status bar (a 48x48 dot bitmap with shades of gray, like in Doom; 26 expressions) shows the health, pain and a grin after a kill; `doom85/faces.py` makes it from a sheet of pictures (the tape in the repo has a drawn face, a local `faces_local.bin` is not committed). From the second level some monsters shoot fireballs along straight lines (you can step out of the line); key 1 switches to a knife (close range, no ammo, hits half of the time), key 2 to the pistol. The monsters patrol the corridors, chase you along the shortest path when you are closer than 10 cells (path length) and growl when they are near. The pistol is a bitmap with an outline, it kicks up with a muzzle flash when fired. There are five difficulty levels (chosen on the title screen with 1 to 5 or the arrows and Space: 2 to 6 monsters with 1 to 4 hit points, faster and farther chasing, fewer rounds). By default 30 rounds and 5 ammo packs (small crates, +10) to find in the maze; at 0 the HUD says BEZ NABOJOV. There are sound effects (shot, hit, kill, pickup, hurt), a melody on the title screen and a quiet melody in the game (off by default, key M toggles it, because the tempo follows the frame rate). It runs at about 7 frames per second (measured by cycle count in the simulator, not on the Pi) and draws straight into the
visible video memory, so the picture is built from left to right. Details: [doom85/README.md](https://github.com/jose1711/pmd85console/blob/pi-deploy/doom85/README.md).

### Programs from the upstream simpmd, pmd85.uk and schotek.cz

The old upstream repository (`ceresek/simpmd`, tapes of 2009 to 2012) has the tape directories `editors`, `languages`, `utilities` and `data` that this repo did not carry.
Added (all started in the emulator and checked on the screen, the keys come from the menus of the programs): the editors `TEXTEDIT`, `IPOWORD`, `FLARE+`, `KARTOTEK`, `DAT-PMD`, the graphic
editors `GREDIT`, `GRED`, `GREP`, `GRAFIKA`, `VEGRAP3X`, `VGRAPF` (the last three also from `pmd85.uk/pmd85_dos`, the DOS emulator of P. Chrenko), `ELEKTRO`, `MUSICA` and, in the
category `Programovani`, the assemblers and debuggers `DDT`, `MRS`, `SYSTEL`, `DAM+2`, `DAM+9000`, `VMON`, `VMON9000` and the BASIC program `TESTLOG`.
Not added: the copy utilities (`COPY`, `COPPY`, `COPYPLUS`, `OPTIMAX`, `TRANSFER`, `WELL+2`: they are for real tapes), the pictures and melodies of `data` (they need
GR or MUSICA to be loaded), `KASWORD` (the header says autorun at `FFFF`, the program does not start), the source files of `DAM+2.*` and the other copies of the games
(`pmd85.uk`, `pmd85.topindex.sk` = firmware of a hardware clone, `schotek.cz`: the same tapes; schotek has four other copies with the loader names `FRED +5`, `FLAPPY+S`,
`BOULD +S` and `PENETR.S`, they run the same games on the models 1, 2 and 2A).

### DIKTATOR

Text/economic game Diktátor (Don Priestley, DK'Tronics 1983, Czech translation František Fuka, FUXOFT) as a port of the ZX
Spectrum program for the PMD 85-2A/3 (AllRAM and ColorACE, the readme of `diktator01.zip` of
<http://busy.speccy.cz/tmp/diktator01.zip>, mentioned in the forum thread "Diktátor" of pmd85.borik.net). The file
`Diktator01.cod` (29 401 bytes) is one block loaded at `0x0000` and started there (`DI`, `LXI SP,0`, the AllRAM switch
`MVI A,82H / OUT F7H`); the tape of this repo is made of it (the header `00 3F 00 00 D8 72 DIKTATOR`, the checksums), the
list loads it with `MGLD 00, JUMP 0000`. It is a ZX BASIC program in a Z80 interpreter, it fills the screen with the title for about a
minute and then shows the flag of the Ritimban Republic. The colours of ColorACE are not emulated. The other game
of the thread, `chamurabi.ptp` (Hammurabi), is on ulozto.cz only and is not here.

### PODRAZ

Text game in BASIC (an unemployed programmer in New York moves money with a computer and a modem), `podraz.ptp` of the
forum thread "Podraz 3" of pmd85.borik.net (Roman Bórik found it on the Zbrojováček disks, it is not an exact copy of
the Podraz 3 of the ZX Spectrum). The `.ptp` has only the 2 byte frame of the tape cut off after the header (the header
and the checksum of the data are valid), the program starts at `0x2401` with the line 2, so it works without the second cut
that the BASIC programs of `basic.zip` need. It runs in BASIC-G V2.0 (`ROM basic2.rmm, FONT, TYPE LOAD 0, TYPE RUN`).

### SPINDIZZY (RM-TEAM, PMD 85-2A)

Port of the ZX Spectrum game Spindizzy (Electric Dreams 1986) by RM-TEAM (Roman and Martin Bórik, 2013), the tape block
`SPINDZ` of `games-rm-team.ptp` (`pmd85emu_games_rm-team.zip`, <https://pmd85.borik.net/wiki/Spindizzy>). It needs the
64 kB RAM of the PMD 85-2A and checks the paging of the RAM under the monitor ROM (`MODEL` `2A`; with `2` it prints
`64K RAM IS REQUIRED FOR RUNNING OF THIS GAME!`). The block has the two 2 byte frames cut off like the other packages of
RM-TEAM (the one after the header and the one after the checksum of the loader), the same tape works with `MGLD 00`.
Keys: O/P left and right, Q/A up and down, [Space] fire, N freeze, T pause, M map, S score, C shape, F1-F4 the view.
The same package has two more games, added the same way (the two frames cut off, `MGLD 00`):

- `HIGHENC`: Highway Encounter (Vortex Software 1985, port RM-TEAM 2016, <https://pmd85.borik.net/wiki/Highway_Encounter>),
  `MODEL` `2A` (it needs the RAM paging like Spindizzy). The menu: `5` starts, `1` keyboard, `2` joystick, `3` the
  information with the keys (also on the screen), `4` demonstration. Keys: O left, P right, Q fast, A slow, [Space] fire,
  H hold, T and C abort the game. The gamepad: D-pad, A = [Space], X = H, Y = `5`.
- `KUBANOID`: Kubanoid (Stardust 2025, port RM-TEAM and X-Agon 2026, <https://pmd85.borik.net/wiki/Kubanoid>), a logic game
  of falling dice, `MODEL` `1` (it works on all models). The menu: `S` starts, `K` keyboard, `J` joystick, `D` redefines
  the keys, `F` colours, `T` the target, `H` music (the MIF 85 card is not emulated). Default keys found by trying: O left,
  P right, Q and A turn the dice, [Space] the fast drop; the pause, the music and the end are defined by `D`. The gamepad:
  D-pad, A = [Space], Y = `S` (the first screen needs a key, the high score table is not kept).

### Icons

Icons left of the name show what the game can be played with besides the keyboard
(the legend is at the bottom of the list):

| Icon | Meaning |
|---|---|
| gamepad | the buttons of a gamepad are enough, see [remote.md](remote.md#gamepad) |
| TV remote | the TV remote control over HDMI-CEC is enough (cursor keys, OK, digits), see [remote.md](remote.md#tv-remote-control-hdmi-cec) |
| disk | a saved state of the game exists (`states/<game>.sav`, see [usage.md](usage.md#saved-state-experimental)); shown in the list, on a category row when one of its games has one, and under the four games of the main menu |

A game without icons is keyboard only (text adventures, games that need many action keys) or its controls are not
known yet.

## Format of `games.lst`

One line per tape, columns separated by `;` (do not use `;` inside a column), `#` starts a comment.
The games are listed alphabetically by `NAME` whatever the order of the lines.

```
NAME;MODEL;NOTE[;REMAP[;PAD[;KEYS[;INPUT[;CATEGORY]]]]]
```

| # | Column | Meaning |
|---|---|---|
| 1 | `NAME` | the tape file in the share directory |
| 2 | `MODEL` | `1`, `2` or `2A`: the PMD 85-1 ROM `M1`, the PMD 85-2 ROM `M2-Patched-Tape`, or the PMD 85-2A (the same ROM with the AllRAM paging, see below) |
| 3 | `NOTE` | shown in the list, the commands to type (`MGLD 00, JUMP 2000`) |
| 4 | `REMAP` | host keys mapped to other PMD 85 keys for this game |
| 5 | `PAD` | buttons of the web remote control for this game |
| 6 | `KEYS` | free text with the controls, shown under the list (without diacritics) |
| 7 | `INPUT` | devices besides the keyboard: `P` gamepad, `R` TV remote |
| 8 | `CATEGORY` | a sub list of the games list (see below), empty for the games of the main list, several categories separated by `\|` (e.g. `Deskove hry\|2 hraci`; written without the backslash in the file) put the game in each of them |

### `CATEGORY`

The games list shows the categories first, as rows `[ Name ] (number of games)`, then the games without a category.
[Enter] opens a category, its first row `(o uroven vys)` (or a held [Esc]) goes back to the main list. A category exists
as soon as a game names it (the order is the order of the first game in the list); the name is shown as it is
written, without diacritics. The columns before it must be there, empty if needed (`ZMRZLINY;2;MGLD 00;;;;;Pro nejmensi`).
The order of the categories is the order of the first game of each, or fixed by lines `#CATEGORY Name` in the head of
`games.lst`. The web remote control gets the same rows. Now: `Pro nejmensi` (DOMCEKY, POSTARIK, ROBOTIK, ZMRZLINY, SASKO), `Dema`
(FERLAJT, KANKAN), `Programovani`, `Deskove hry`, `2 hraci` and the families below.

A category can be inside another one: `Parent/Name` (e.g. `Programovani/KAREL`) is a sub list of `Programovani`; the parents
are created by themselves, [Enter] opens it, `(o uroven vys)` or a held [Esc] goes one level up (the cursor on the row that
was left).

#### Families (the versions of one game)

A line `#FAMILY Name` in the head of `games.lst` declares a category that is a family: the versions of one game (MANIC with
MANIC+, MANIC+2, MANIC-1, MANIC-2, MANIC23C). The versions name it in `CATEGORY`. Unlike the other categories, the row of a
family is not at the top of the list, it stands among the games in alphabetical order, shown as the name with a filled
triangle (`MANIC ▶`, the web remote control `MANIC >`) instead of the brackets and the count of a category. A family of a category
has the name with its parent (`#FAMILY Programovani/KAREL`, the games of it `...;Programovani/KAREL`). Now: MANIC, FLAPPY,
ARKANOID, BOULDER, PAMPUCH, PONTRIS, KVADRO, WILLY, GALAXIA, MTRON, MYTH, `Deskove hry/PEXESO`, `Deskove hry/REVERZ`, `Programovani/ZOFKA`,
`Programovani/KAREL`, `Programovani/KAREL3D`. A family with one version is not needed, use the game itself.

### PMD 85-2A (`MODEL` `2A`)

The PMD 85-2A has 64 kB of RAM, and the monitor ROM at `0x8000`-`0x8FFF` can be switched off to give the RAM under it
(AllRAM, the 8255 of the system, PC4: 1 the ROM, 0 the RAM; a write goes to the RAM also while the ROM is mapped). The
ROM `M2` of this emulator is the monitor of the PMD 85-2A byte for byte (`monit2A.rom` of the ROM package of
pmd85.borik.net, it differs from the PMD 85-2 `monit2.rom` only in `HEADIN` at `0x8EBE`); `M1` is `monit1u.rom`.

- The paging: the control word `0x8A` makes the upper port C an input (PC4 has a pull-up, the ROM stays, the monitor does
  it), `0x82` an output with the latches cleared (the RAM), then `0x09` or `0x08` (bit set/reset) or a write to port C change
  PC4; the saved state of a game keeps the mapping and the 4 kB under the ROM.
- The model `2` stays as it was (64 kB of RAM writable everywhere, the ROM is not switched).
- `basic2A.rmm` (BASIC-G V2.A, 9 kB, more memory for the programs) is in `simpmd/data/roms`, use it as `ROM basic2A.rmm`.
- Not done: the colours of the PMD 85-2A (bits 6 and 7 of a byte of the video RAM choose one of 4 colours of six pixels).

Tested with a small program that writes to the RAM under the mapped ROM and reads it with the ROM and with the RAM
(`23 55 23 AA 23` as it must be, the model `2` gives `55 55 AA AA AA`) and with BOMBARDE in BASIC-G V2.A.

### `REMAP`

`HostKey=PmdKey,...` with SDL key names, e.g. `Left=Left Shift,Right=V`.
It applies to the keyboard, the gamepad and the browser alike.

- AUTO and ZED steer with [Shift] (left) and [V] (right) on the PMD 85, here with the cursor keys.
- MANIC: [Space] is mapped to [Shift] (jump), so the D-pad and the jump are on different hands.
- MANIC with a remote control: `Up=@Left Shift,Down=A`. [Up] jumps in the direction of the walk: press [Left] or
  [Right] first, then [Up] (within 400 ms of releasing the arrow); standing still, [Up] jumps straight up. The `@`
  before the target key means: also press the cursor key last used. A remote control can not press two keys at once.
  OK stays [Enter] (start of the game, end of the initials) and [Down] types `A`, so the initials after a game
  ("AAAAA") can be typed with the remote.

### Gamepad keys per game in `REMAP`

`pad:<input>=<key>` sets the key of a gamepad input for this game only (the rest stays as without it; it is not
changed by the other `REMAP` items, they apply to host keys). The inputs: `up`, `down`, `left`, `right`, `a`, `b`, `x`,
`y`, `lb`, `rb`, `back`, `start`, `lt`, `rt` (the triggers), `ls`, `rs` (the stick clicks); the triggers and the
stick clicks have no key by default. Example (the games for the youngest): `Left=F1,Down=F2,Up=F3,Right=F4,pad:left=F1,
pad:down=F2,pad:up=F3,pad:right=F4,pad:a=Space,pad:b=F5,pad:start=Return,pad:back=F12,pad:ls=Keypad +,pad:rs=Keypad +`.
The triggers work for a pad that SDL knows as a game controller.

### Short, long and held keys in `REMAP`

`HostKey=Short|Long[|Held]` (any key of `REMAP`):

| Part | When it is sent |
|---|---|
| `Short` | a press shorter than 450 ms: sent when the key is released, held for 150 ms (a game that scans the keyboard slowly still sees it) |
| `Long` | a press longer than 450 ms, instead of `Short` |
| `Held` | optional, at once when the key goes down and until it is released (a jump must not wait for the release); `@Key` also presses the cursor key last used (held, or released less than 400 ms ago) |
- INVADERS: [End] / [PageDown] are the fast arrows, the digits `0`-`3` are on the gamepad.

### `PAD`

`Label:key,...` (SDL key names), e.g. `Left:Left,Fast left:End,Fire:Space,0:0`.

- `key&key` presses two keys at once (a mouse presses only one button), e.g. `Jump left:Left Shift&Left` in MANIC.
- Games without `PAD` get the default set: E, Y, cursor keys, Space, Enter, H, I, Shift, RCL, Esc.
- The menus get a short set.
- The keys known from the code of the games are in `games.lst` (INVADERS: fast arrows, fire and the difficulty
  digits; AUTO, ZED, MANIC, SASKO, ...).

### `KEYS`

Free text shown under the list for the selected game. "neznamo" is shown for games without it
(many games do not print their keys).

### `INPUT`

- `P`: the gamepad is enough (its buttons: D-pad/E/Y, Space, Enter, H, I, Shift, RCL).
- `R`: the TV remote is enough. The rule used to fill it: the game needs at most 5 buttons (the keys of `PAD`
  without [Esc] and without the keys that `REMAP` maps onto) and all of them are on the remote (cursor keys,
  OK = Enter, digits). A game that asks for letters (name or initials for the high scores, passwords, commands)
  never gets `R`, whatever the number of buttons; see [remote.md](remote.md#tv-remote-control-hdmi-cec).

## Notes on games

### INVADERS

- The ship moves by 3 steps with the fast arrows [End] and [PageDown] (the keys LLEFT and RRIGHT of the PMD 85);
  the gamepad [LB] and [RB] send them.
- The difficulty is `0`-`3` (4 is not playable). On the gamepad: D-pad up = 0, D-pad down = 1, [X] = 2, [Y] = 3.
  The TV remote plays in a limited mode: OK (Enter) is remapped to [Space], so it fires, and the cursor up and down
  select the difficulty `0` and `1` (the tested TV does not send the digits over CEC, so `2` and `3` are only on the
  gamepad). The fast arrows are not on the TV remote either.

### MANIC

- Loads with `MGLD 02` (MANIC+) or `MGLD 00` (MANIC-1).
- Cursor keys, [Shift] (jump), [S], [F1]; [F1] is a menu key.

### KAREL, KAREL3D

The games have no manual, the commands are in [README.karel.md](https://github.com/jose1711/pmd85console/blob/pi-deploy/README.karel.md).

### MESTO (Mesto robotu)

A text adventure. It asks for a password after the intro screens: `konvalinka` (case does not matter).
The game body is encrypted with a key made of the password (the password is not stored anywhere),
so a wrong one starts garbage.

### BOULDER

The second `H` may be lost when typing, `PMD_KEY_MS=400` makes it pass.

## Screenshots

Captured with `PMD_SCREENSHOT` (see [usage.md](usage.md)).

| FLAPPY | FRED | BOULDER DASH (game menu) |
|:------:|:----:|:------------------------:|
| ![FLAPPY](https://github.com/jose1711/pmd85console/raw/pi-deploy/screenshots/flappy.png "flappy") | ![FRED](https://github.com/jose1711/pmd85console/raw/pi-deploy/screenshots/fred.png "fred") | ![BOULDER](https://github.com/jose1711/pmd85console/raw/pi-deploy/screenshots/boulder.png "boulder") |
