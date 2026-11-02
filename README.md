# simpmd

Emulator of the Tesla PMD 85 family of 8-bit computers (Intel 8080 at 2 MHz, 48 kB RAM): PMD 85-1, PMD 85-2 and
PMD 85-2A. Written by Petr Tůma (2008, [ceresek/simpmd](https://github.com/ceresek/simpmd)); this tree is the SDL2
version with a game menu, developed for the
[PMD-85 console](https://github.com/jose1711/pmd85console) (a Raspberry Pi with a TV) and usable on a PC.

The original SDL 1 version is kept under the tag `sdl1-final`.

## Features

- SDL2 / SDL2_ttf, runs on a PC or on a Raspberry Pi (KMS/DRM, no desktop).
- A menu of the games and programs in [`data/games.lst`](data/games.lst): the load commands (`MGLD`, `JUMP`, `POKE`,
  typed BASIC-G commands) are typed automatically. Categories, families of versions, icons for the keyboard, gamepad
  and TV remote.
- Models PMD 85-1, 85-2 and 85-2A (all RAM mode, ROM switched off by port C), BASIC-G in a ROM module,
  Czech and Slovak letters of the PMD 85-3 for the BASIC programs.
- Control by keyboard, gamepad, TV remote (HDMI-CEC) or a browser (`PMD_WEB`), per game key remapping (`REMAP`).
- Saved state of a game (Alt/Ctrl + F5 save, F9 load), experimental.
- Sound with a DC blocker and a buffer lead, tape input and output, screenshots, test and debug variables (`PMD_*`).
- About 175 tapes and games.lst entries: games, demos, programs, the BASIC programs, and two programs written for this emulator
  (`MPY85`, a subset of Python in 8080 assembly, and `DOOM85`, a raycaster). Sources:
  [mpy85](https://github.com/jose1711/pmd85console/tree/pi-deploy/mpy85),
  [doom85](https://github.com/jose1711/pmd85console/tree/pi-deploy/doom85).

## Build and run

Needs SDL2, SDL2_ttf and a C++11 compiler (Debian/Ubuntu: `libsdl2-dev libsdl2-ttf-dev g++`, Arch: `sdl2 sdl2_ttf`).

```sh
make                                  # debug build, data are read from the current directory
make install-data PMD_SHARE=bin/      # ROMs, font, logos, games.lst and tapes into bin/
cd bin && ./simpmd
```

Release build with a fixed data directory (`PMD_SHARE` is compiled in, trailing slash required):

```sh
make PMD_BUILD=RELEASE PMD_SHARE=$HOME/share/
make install-data PMD_SHARE=$HOME/share/
~/path/to/bin/simpmd
```

Menu: [Left] / [Right] select, [Enter] starts a game, [Up] or [E] opens the list of all games, hold [Esc] to quit.
Details in [INSTALL](INSTALL).

## Documentation

| Topic | File |
|---|---|
| Running the emulator, test mode, all `PMD_*` variables | [doc/usage.md](doc/usage.md) |
| Games list, `games.lst` format, notes on the games | [doc/games.md](doc/games.md) |
| PC keyboard mapping and the PMD 85 keys | [doc/keyboard.md](doc/keyboard.md) |
| Gamepad, TV remote control, web remote control | [doc/remote.md](doc/remote.md) |
| Changes | [CHANGELOG.md](CHANGELOG.md) |
| Authors, thanks | [CREDITS](CREDITS), [ABOUT](ABOUT) |

## Data

```
data/roms/   ROM monitors M1 (PMD 85-1), M2 (PMD 85-2/2A) and the BASIC-G modules (*.rmm)
data/font/   font of the menu
data/logo/   logos and splash images
data/tapes/  game tapes, games-pmd1 and games-pmd2
data/games.lst   the games list (format: doc/games.md)
```

`make install-data` copies all of it into one flat directory, which is what the emulator reads.
The tape `DOOM85` uses generic faces in the status bar; a build with other faces is described in the `doom85` sources.

## License

See [COPYING](COPYING) and [CREDITS](CREDITS). The tapes are the work of their authors; the authors of the
programs are listed in [ABOUT](ABOUT).
