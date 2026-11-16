# Keymap

`keymap.exe` remaps the Brain's keyboard and shows what each key does. It picks the layout from the Brain's profile (`HKLM\Software\Brain` `Profile`, as bstart does):

| Profile | Layout |
| --- | --- |
| `pwsh2`, `pwsb1` | PW-SH2: Symbol and Numbers layers and key remaps. The PW-SB1 uses it unchanged |
| `pwa7300` (also the PW-G5300) | PW-A7300: a UK layout with Shift and Fn, shown on the sub screen |
| `pwsh4` | PW-SH4: the PW-SH2 layers on its keys, see [below](#pw-sh4) |

Without a known profile it does nothing.

Every profile puts `pwsh2-boot/Keymap.exe` on the card, with `pwsh2-boot/PROFILE/Windows/StartUp/Keymap.lnk`, so Explorer starts it at boot, and `pwsh2-boot/PROFILE/Windows/Programs/Keyboard Help.lnk` in the Start menu. Only a program that runs `\Windows\StartUp`, such as the default Explorer, starts it.

A tray icon shows the current layer: `ab`, `Ab` (Shift for the next key), `AB` (Shift held), and the model's layers. Tap it to show or hide the keyboard map, a picture of the keyboard with what each key gives in each layer. Start > Programs > Keyboard Help (`keymap.exe help`) does the same.

Running Keymap again quits it. `keymap.exe log` writes every key event to `\Storage Card\keymap.log`.

## PW-SH2 and PW-SB1

| Key | Does |
| --- | --- |
| Symbol (held) + letter | Symbols, blue on the map; with Shift, red |
| Numbers (held) + top row | `1`-`0`; with N, M and -: `,` `.` `-`; other letters give the red symbol |
| Symbol + Up, Down | System volume, 16 levels, saved for Volume & Sounds |
| Symbol or Numbers + arrows | Page Up, Page Down, Home, End |
| HOME | Backspace |
| Del | Enter |
| Enter | Space |
| Space | Windows key (Start menu) |
| PgUp, PgDn | Ctrl, Alt |
| Search | Escape |
| Back | Tab |
| Marker | Open Brain Control, or bring it to the front if it is open |
| Tool | Show or hide the keyboard map |

Ctrl, Alt and Tab chords with Shift send a real Shift.

The keyboard map key is Tool, left of HOME: pressing it shows the map over everything, and pressing it again or tapping the map hides it. Releasing the key leaves the map up.

## PW-A7300 and PW-G5300

It needs the profile's `keybd_EDNA2.dll`, which sends 機能 as a plain Shift, and brain-drivers' `EDNA2_slcd_ST7586.dll` for greys, the overlay and sub screen brightness.

| Key | Does |
| --- | --- |
| 機能 | Shift, held or tapped once for the next key |
| 音声 | Fn, held or tapped once for the next key |
| ー | Space; Fn gives `-` |
| 前見出, 次見出 | Ctrl and Alt; Fn gives Page Up and Page Down |
| Sジャンプ | Tab |
| 後退 | Backspace; Fn gives Delete |
| Fn + letters | `1`-`0`, `( ) [ ] + - * / %`, `~ ? , . ; & '`, and `=` on Enter |
| Shift + Fn + letters | `! " £ $ % ^ & * ( )`, `{ } < > = _ @ \ #`, `` \| ` € ¬ : ! @ `` |
| Fn + Left, Right | System volume, 16 levels, saved for Volume & Sounds |
| Fn + Up, Down | Show the volume bar; while it shows, switch it to main screen or sub screen brightness, which Fn + Left and Right then change |

The sub screen shows the layer the next key will use, black and with its backlight on while a modifier is active. Main screen brightness uses the five levels Brain Control offers for Sharp's driver and is saved to the profile's `display.txt` for bstart. Sub screen brightness is saved by the sub screen driver.

While an app has claimed the touch panel from the touchpad driver (`PAD1:`), Keymap leaves the sub screen to it and shows the volume and brightness bar with the sub screen driver's overlay, which puts the app's picture back afterwards. The device codes follow brain-drivers' `sublcd_ioctl.h` and `touchpad_ioctl.h`.

The keyboard map's key positions are approximate. It has no keyboard map key by default; a `keys.txt` line such as `F7=help` adds one.

## PW-SH4

The PW-SH2 layers and handling, on the PW-SH4's keys. Its keys send the PW-SH2's codes where they correspond: Shift `VK_CAPITAL`, 記号 F12 (Symbol), 音声 F11 (the PW-SH2's Numbers code), top row F1-F4, F7-F10. Codes recorded with `keymap.exe log` on the device; positions from a photo.

| Key | Does |
| --- | --- |
| 記号 (held) + letter | Symbols, as on the PW-SH2 |
| 音声 (held) + Q-P | The digits printed on them; with N, M and -: `,` `.` `-` |
| 音声 + arrows | Page Up, Page Down, Home, End |
| 記号 + Up, Down | System volume |
| PgUp, PgDn | Ctrl, Alt, as on the PW-SH2 |
| 調べる | Escape |
| 戻る | Tab |
| HOME | Windows key (Start menu) |
| 履歴 | Open Brain Control, or bring it to the front |
| ツール | Show or hide the keyboard map |

音声 is the Numbers layer because it sends the PW-SH2's Numbers code. HOME opens the Start menu: on the PW-SH2 that is Space's job and HOME is Backspace, but the PW-SH4 has its own Space and 削除 (Backspace) keys. PgUp and PgDn are Ctrl and Alt, as on the PW-SH2, despite their vol+ and vol- labels; `PageUp=volume-up` and `PageDown=volume-down` in `keys.txt` make them volume keys.

## keys.txt

`pwsh2-boot/PROFILE/keys.txt`, or `pwsh2-boot/keys.txt`, replaces the layout's own remaps. One remap per line:

```
# KEY=key NAME     send another key
# KEY=launch PATH  start a program
# KEY=help         show or hide the keyboard map
# KEY=brain-control  open Brain Control, or bring it to the front
# KEY=volume-up    raise the system volume (volume-down lowers it)
# KEY=none         ignore the key
F8=launch \Storage Card\Apps\PHM Tools\notepad.exe
```

Names: `F1`-`F12`, `Back`, `Return`, `Space`, `Tab`, `Escape`, `Left`, `Right`, `Up`, `Down`, `Delete`, `Home`, `End`, `PageUp`, `PageDown`, `Windows`, `Ctrl`, `Alt`, or `0xNN`. Up to 32 lines. The layers (Symbol, Numbers, Fn) are applied first.

## Source

The PW-A7300 layout is from [pw-a7300-sublcd](https://github.com/Gadgetoid/pw-a7300-sublcd) `34e1c02` (MIT, Phil Howard) `apps/keymap`, with the parts of its sub screen library Keymap uses (`sublcd.c`, `draw.c`, `font.c`, `backlight.c`). The 5x7 font is the classic glcdfont, as in the MIT-licensed Ameba Arduino SDK. The PW-SH4 key codes and positions are from `keymap.exe log` on the device and a photo of its keyboard. Brightness is saved with brainsurgeon's `display.c` and `config.c`.
