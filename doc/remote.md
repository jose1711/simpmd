# Gamepad, TV remote and web remote control

[← back to README](../README.md)

Three ways to play without the PC keyboard. All of them send ordinary keyboard keys, so `REMAP` of a game
([games.md](games.md)) applies to each of them. The list of games shows with icons which game suits which device.

## Gamepad

A gamepad (USB, or Bluetooth if paired in the OS) works in the menu and in the games. Its buttons are translated
to keyboard keys.

| Pad | Key |
|---|---|
| D-pad / left stick up, down | [E], [Y] (the PMD 85 keys for up and down, host key Z) |
| D-pad / left stick left, right | cursor keys |
| [A] | Space |
| [B], [Start] | Enter |
| [X] | H |
| [Y] | I |
| [LB] | Shift |
| [RB] | keypad plus (RCL) |
| [Back] | Escape (hold: game back to menu, in the menu quit) |

The defaults follow the instructions of the games. A game can change them in `games.lst` (`REMAP`): in MANIC [A]
jumps (Space is remapped to Shift, so the D-pad and the jump are on different hands). INVADERS uses [LB] and [RB]
for the fast arrows and the buttons for the difficulty, see [games.md](games.md#invaders).

Pads without an SDL controller mapping use a common layout (hat or axes 0/1 = D-pad, buttons 0-7 = a b x y lb rb
back start).

### Override the keys

`PMD_PAD` with the inputs `up down left right a b x y lb rb back start` and SDL key names:

```sh
PMD_PAD='up=Up,down=Down,a=Space,lb=Left' bin/simpmd
```

### Bluetooth (not tested)

Only the USB gamepad is tested. The Pi A+ has no Bluetooth, a USB dongle is needed.

- `bluez` must be installed and running: `apt install bluez`, `systemctl enable --now bluetooth`.
- The gamepad is paired once and trusted, so that it reconnects by itself:

```
bluetoothctl
  power on
  agent on
  scan on
  pair <MAC>
  trust <MAC>
  connect <MAC>
```

- The service user needs the `input` group (`pmd85.service` has it).

Not verified:

- whether a gamepad connected after the emulator started is picked up,
- the button mapping of controllers unknown to SDL (check with `evtest`),
- the load of the USB dongle on the weak power supply.

## TV remote control (HDMI-CEC)

The remote control of the TV works over HDMI-CEC: the TV sends the keys to the Pi and the kernel presents them as
a keyboard (`vc4-hdmi` input device).

| Remote | Key in the emulator |
|---|---|
| cursor keys | cursor keys |
| OK | Enter |
| Back | Esc (hold, see below) |
| digits 0-9 | digits, only if the TV sends them over CEC (the TV tested here does not) |

Requirements:

- A TV with CEC switched on and `cec-ctl` (v4l-utils) on the Pi.
- The Pi must be the **active source** of the TV. The TV sends keys only to the active source. The service sends
  *Image View On* and *Active Source* at start (`pmd85-hdmi wake`). If you switch the TV to another input, the remote
  stops working until the service is restarted.

How it works:

- The kernel presents OK as `KEY_OK`, which SDL does not know, so it did nothing. `pmd85-hdmi keymap`
  (a start step of the service) rewrites the keymap of the input device: OK (CEC scancode `0x00`) gives `KEY_ENTER`
  Back (`0x0d`, CEC *exit*) gives `KEY_ESC` and the digits (`0x20`-`0x29`, `KEY_NUMERIC_0`...) give `KEY_0`...`KEY_9`.
- [Esc] has to be held for `PMD_ESC_MS` (800 ms). The remote repeats a held key only for a short time (the hold
  was measured at about 0.8 s), so `PMD_ESC_MS=500` in `~/pmd85.env` is safer on a TV remote.
- Other buttons (coloured, play, context menu = `KEY_EPG`) are not known to the emulator.

Games with a TV remote icon need only the keys above (at most 5 buttons). A game where you type letters can not be
played with the remote: a name or initials for a high score table (MTRON-1/2/3, SP-DEMIS, ZABY, PISQORKY), passwords
(MESTO), commands (KAREL, KAREL3D), chess (SACH).

## Web remote control

`PMD_WEB=<port>` starts a small web server. Open `http://<host>:<port>/?t=<token>` in a browser (PC or phone) to see
the screen of the simulated machine and to play. Keyboard keys and an on-screen gamepad are sent to the emulator
and work in the menu and in the games (up and down are [E] and [Y]).

Set it in `~/pmd85.env`:

```sh
PMD_WEB=8085
PMD_WEB_TOKEN=<secret>
```

Notes:

- `PMD_WEB_TOKEN` is required in the URL (`?t=`). Without a token everybody on the network can control the machine.
- Traffic is plain HTTP, use it on a trusted network only.
- The screen is read from the video memory (12 KB per frame). There is no sound, it plays on the Pi.
- [Esc] in the main menu asks for confirmation, it quits the emulator (the Pi is halted only with `PMD_HALT=1`, see
  [service.md](https://github.com/jose1711/pmd85console/blob/pi-deploy/doc/service.md)). [Esc] in a game returns to the menu.
- The first typed key waits until the simulated processor has run 2 s, so the load command is not lost on a slow Pi.
- The buttons of the page per game are set by the `PAD` column of `games.lst`.
