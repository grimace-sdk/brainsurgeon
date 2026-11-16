# PW-SH4

`BRAIN_MODEL=pwsh4` builds `pwsh4_ce_defconfig` and names the wrapper `edsh4exe.bin`, which the release card carries as `EDSH4EXE.BIN`. It shows the [boot menu](reference.md#menu-and-u-boot-commands).

## Profile

- The `pwsh4` profile adds the brain-display 848x480 driver and the brain-drivers board, backlight and touch shim drivers. The YMU818 volume patch is not used: the codec is a BU26154.
- Sharp's touch driver treats x >= 800 as its soft-key area and clamps x to 799. `profiles/registry/touch-areas-pwsh4.reg` moves the soft-key areas past 848, and `profiles/patches/sx8650-width-pwsh4.json` raises the clamp to 847. The factory calibration already covers the whole 854-column panel. A normal press reaches only about 13 px from the top and bottom edges; the touch shim stretches those last few pixels onto the edge.
- [Keymap](../ce/keymap/README.md#pw-sh4) gives the PW-SH2's Symbol and Numbers layers on 記号 and 音声, Ctrl and Alt on PgUp and PgDn, volume on 記号 + Up/Down, Brain Control on 履歴 and the keyboard map on ツール. Its keys send the PW-SH2's codes where they correspond (recorded with `keymap.exe log`).
- The U-Boot console is on the serial port only.

## Boot

EBOOT `ED-SH4 V100` runs `EDSH4EXE.BIN` from the card without a config file: its download flag defaults to 1 and only a valid `EDSH4CFG.BIN` clears it. The config format differs from ED-SA2 (flags at `+0x20`, develop word `+0x24`, checksum `+0x2c` over 0x30 bytes), so `tools/make-edsa2cfg.mjs` must not be used for it.

BSP arguments are at PA 0x400F3000, internal boot flag `+0x60`, as on the SH2. Slot A is empty; CE is in slot B (`nk-01c5ff5c` for the dumped V100 image, 29,753,180 bytes).

## U-Boot

Upstream `pwsh4` is the SH2 board with an 854x480 ILI9805 panel. With `CONFIG_SHARP_WINCE`, `pwsh4.c` skips the early clock changes, sets the SSP clocks at MMC init and leaves GPIO 2_21 alone (upstream drives it as SD power, copied from the SH2; on the SH4 it is a keyboard column).

The menu is drawn on the 480x854 panel as EBOOT left it (MADCTL 0xD0, splash at PA 0x40100000), as on the PW-SH2 with 854 panel pages (`SHARP_PANEL_PAGES`). Every write is a full frame (`SHARP_PANEL_FULL_FRAMES`). The progress bar is redrawn at most every 200 ms.

The keyboard is a GPIO matrix, not the I2C controller. `CONFIG_SHARP_KEYBOARD_MATRIX` builds `keymatrix.c` instead of `keyboard.c`. `pwsh4.c` lists the menu keys by column and row (GPIO bank * 32 + pin). Each read drives the key's column low and reads its row, and restores both pins. A key is skipped if its row is low before the column is driven. A key down for two 20 ms polls is a press; a key already down at the first scan counts as held at power-on. Layout from Brainux's `imx28-pwsh4.dts` and `brain-kbd-gpio.c`.

| Key | Column | Row |
| --- | --- | --- |
| Enter (決定) | 4_8 | 4_6 |
| Space | 2_18 | 4_6 |
| Up | 2_19 | 4_6 |
| Right | 2_20 | 4_6 |
| Down | 2_21 | 4_6 |
| Left | 2_20 | 4_7 |
| Back (戻る) | 2_19 | 4_7 |

`brainkey` drives the Enter column low and needs the Enter row low for three 20 ms polls. EBOOT leaves both as GPIO inputs.

The layout differs from the SH2. Bottom block, left to right: 音声 (Sound) over the microphone; page up (volume +) over page down (volume -); 記号 (Symbols) over スペース/変換 (Space); 戻る (Back); the arrows; 決定 (Enter). There is a real Space key. Page down sits left of Space.

`bootcmd`: `brainmenu; savelog`.
