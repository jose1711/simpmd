# Running the emulator

[← back to README](../README.md)

## Normal start

```sh
bin/simpmd
```

Logo, then the menu. Select a game with [Left] / [Right], start it with [Enter] (the load command is typed
automatically), quit by holding [Esc]. In the menu [Up] or [E] opens the list of all games, see [games.md](games.md).

`PMD_SHARE` (the data directory) is compiled in, see [INSTALL](../INSTALL).

## Test mode

Skip the logo and the menu and start a game directly, optionally typing keys:

```sh
# Start a game of the menu, index: 0 FLAPPY, 1 BOULDER, 2 MANIC+, 3 FRED
PMD_AUTOSTART=0 bin/simpmd

# Start any game of games.lst
PMD_GAME=BLUDISTE bin/simpmd

# Start and load a game with no keyboard needed (tested examples)
PMD_AUTOSTART=0 PMD_KEYS='MGLD 03|____H__ ____' bin/simpmd   # FLAPPY: load, [H] play, [Space] skip controls
PMD_AUTOSTART=3 PMD_KEYS='MGLD 03|____H__ ____' bin/simpmd   # FRED: load, [H] play, [Space] skip info
PMD_AUTOSTART=1 PMD_KEYS='MGLD 03|________H' bin/simpmd      # BOULDER: load (~20 s), [H] game menu
```

Notes on `PMD_KEYS`:

- The keys after `MGLD 03|` depend on the game. Add `_` pauses so the tape load finishes first
  (BOULDER ~20 s, FLAPPY and FRED ~8 s).
- MANIC+ loads with `MGLD 02` (not 00 or 03), [Enter] starts the game:
  `PMD_AUTOSTART=2 PMD_KEYS='MGLD 02|____________|' bin/simpmd`.
- BOULDER: the second `H` may be lost, `PMD_KEY_MS=400` makes it pass.
- `PMD_GAME` types the load command from `games.lst` by itself, `PMD_KEYS` replaces it.

## Saved state (experimental)

**Experimental, not verified in real play.** The sound after loading a state was not checked by ear, and a load during
tape reading is not meant to work (the tape position is not saved).

The state of the running game can be saved and loaded again:

| Key | Action |
|---|---|
| [Alt]+[F5] or [Ctrl]+[F5] | save the state of the game |
| [Alt]+[F9] or [Ctrl]+[F9] | load the saved state |

[Ctrl] is the STOP key of the PMD 85 (a game or the monitor stops at it, BLUDISTE and FLAPPY use it). The [Ctrl] of the host
goes to the PMD 85 only after 120 ms without F5 or F9, so [Ctrl]+[F5] and [Ctrl]+[F9] do not stop the game (STOP reaches
the game 120 ms later than a plain [Ctrl] press). [Alt] has no such delay, but a window manager may take [Alt]+[F5].

- The file is `<game>.sav` in `PMD_STATEDIR` (default: `states` in the share directory).
- Saved: the processor registers, the output latches, the clock and the 64 KB of memory. Not saved: the tape position
  and the keys held on the host.
- There is no gamepad, TV remote or web button for it yet, only the keyboard.
- Test mode: `PMD_STATE_SCRIPT="s5000,l9000"` saves (`s`) and loads (`l`) at the given simulated times in ms.

## Headless (no monitor)

Video goes to the software renderer and sound to the file `sdlaudio.raw`:

```sh
cd /tmp
PMD_AUTOSTART=0 PMD_KEYS='MGLD 03|____H__ ____' PMD_SCREENSHOT=/tmp/game.bmp PMD_SCREENSHOT_MS=34000 \
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=disk bin/simpmd
```

## Environment variables

### Start and test

| Variable | Meaning |
|---|---|
| `PMD_AUTOSTART=<0-3>` | Skip the logo and the menu, start the selected game. |
| `PMD_GAME=<name>` | Skip the logo and the menu, start a game of `games.lst`. |
| `PMD_AUTOLOAD=0` | Do not type the load command of the game automatically (default on). |
| `PMD_KEYS=<text>` | Type text on the PMD 85 keyboard, see below. |
| `PMD_KEY_MS=<ms>` | Key press/release time of `PMD_KEYS`, default 100. |
| `PMD_TAPE_IDLE_MS=<ms>` | Idle time of the tape for `~` (the autoload of `JUMP`), default 700. |
| `PMD_SCREENSHOT=<file>` | Save the screen as BMP after `PMD_SCREENSHOT_MS` and exit. |
| `PMD_SCREENSHOT_MS=<ms>` | Screenshot delay, default 3000. |
| `PMD_KEYLOG=<file>` | Log keyboard and window events, paint duration and queue lag. |
| `PMD_KBD_HOLD_MS=<ms>` | Minimal time a key stays pressed in the emulated keyboard, default 60; the game can set another one with `KEYHOLD <ms>` in its note of `games.lst` (BOMBARDE 170: its `INKEY` reads a key once in a pass of the loop of about 150 ms and a shorter tap would be missed, in VOTRELCI such a long hold moves the gun by 2 positions); this variable overrides both. |
| `PMD_PAINTSTATS=1` | Debug: every 10 s the number of paint calls, painted frames, the slowest paint and the longest gap between paints (stderr). |
| `PMD_PAINTTRACE=1` | Debug: the position of the leftmost lit pixel in lines 206-215 at each painted frame. |
| `PMD_PADDLETRACE=1` | Debug: ARKANOID paddle sampler (2 ms): edge, `x`, speed, ball x, key bits and carry of the speed routine. |
| `PMD_SCANTRACE=1` | Debug: moments the emulated program starts to scan the keyboard (`SCAN`) and sees a pressed key (`SEEN`). |

The characters of `PMD_KEYS`:

| Character | Key |
|---|---|
| `\|` | [Enter] |
| `_` | a pause of 2 s |
| `<` `>` | cursor left and right |
| `^` | [Shift] |
| `E` `Y` | up and down in the games |
| `~` | wait until the tape stops loading (`PMD_TAPE_IDLE_MS` without a read) |

### Display

| Variable | Meaning |
|---|---|
| `PMD_SCALE=<n>` | Window size multiplier (288x256 * n), default 3; the window is resizable. |
| `PMD_ASPECT=<shape>` | `stretch` (default, fills the window), `square` (square pixels) or `4:3` (original PMD 85 monitor); the rest of the window is black. |
| `PMD_REFRESH=<ms>` | Screen paint period, default 40; only changed lines are decoded and uploaded. |
| `PMD_RES=<w>x<h>` | Video mode of the menu, logo and fullscreen (must be offered by the display). |

### Keys and input

| Variable | Meaning |
|---|---|
| `PMD_ESC_MS=<ms>` | Hold time of [Esc] (back from a game or the list, quit in the main menu), default 800, 0 = at once. |
| `PMD_PAD=<input>=<key>,...` | Gamepad keys, see [remote.md](remote.md#gamepad). |
| `PMD_WEB=<port>` | Remote control from a browser, see [remote.md](remote.md#web-remote-control). `PMD_WEB_TOKEN=<secret>` protects it. |

### Sound and timing

| Variable | Meaning |
|---|---|
| `PMD_BEEPSLOW=1` | The monitor beep delay of the games (note length, e.g. INVADERS hits) as in the PMD 85-2/2A ROM. |
| `PMD_SNDSTATS=1` | Print sound underrun statistics on exit; `2` also logs each underrun/drop with its time to stderr. |
| `PMD_SNDLEVEL=1` | Log every 10 s: the peak output level, the underruns, the changes of the speaker register and the calls of the sound generator. Tells a silent game (`peak 0`, no changes) from a stopped emulation (no calls) and from a silent TV (`peak` > 0). |
| `PMD_SNDLEAD=<ms>` | Sound lead kept in the buffer, default 50; the buffer is refilled after an underrun. |
| `PMD_SNDRATE=<Hz>` | Sound sampling rate, default 48000. |
| `PMD_SNDBUF=<ms>` | Sound hardware buffer, default 50 (rounded up to a power of two of samples). |
