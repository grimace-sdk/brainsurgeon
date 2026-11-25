# PW-SH3 and PW-SA3

The PW-SH3 and PW-SA3 are the same Brain with different dictionary contents on Sharp's side, so one profile, `pwsh3`, serves both. `BRAIN_MODEL=pwsh3` builds `pwsh3_ce_defconfig` and names the wrapper `edsa3exe.bin`, which the release card carries as `EDSA3EXE.BIN`. It shows the [boot menu](reference.md#menu-and-u-boot-commands).

The hardware is close to the PW-SH4's: an 854x480 panel, a GPIO matrix keyboard and a BU26154 codec, with the same board code in u-boot-brain and Brainux. Only the key positions differ, and Sharp's CE image is its own build.

## Profile

- The `pwsh3` profile uses the PW-SH4's brain-display 848x480 driver and brain-drivers board, backlight and touch shim drivers, in place of Sharp's `ddraw_mx28.dll`, `touchraw.dll` and `backlight.dll`. Sharp's CE draws 800 columns on the 854-column panel and leaves a black strip at the right edge; the 848-pixel desktop fills it.
- Sharp's touch driver treats x >= 800 as its soft-key area, as on the PW-SH4. The profile applies the SH4's `touch-areas-pwsh4.reg` and `sx8650-width-pwsh4.json`, which move the soft-key areas past 848 and raise the clamp to 847.
- The YMU818 volume patch is not used: the codec is a BU26154.
- Not in the profile yet: Keymap (its key tables are for the PW-SH2, PW-SH4 and PW-A7300 layouts), Brain Control's model detection (the PW-SH3 shows as an unknown model) and `autorotate.exe` (tablet mode is untested).

## Boot

EBOOT `ED-SA3` runs `EDSA3EXE.BIN` from the card when `EDSA3CFG.BIN` asks for it. The release's `EDSA3CFG.BIN` is the ED-SA2 file (`make-edsa2cfg.mjs EDSA3CFG.BIN 0xffffffff 0x22`), which a PW-SA3 accepts.

BSP arguments are at PA 0x400F3000, internal boot flag `+0x60`, as on the PW-SH2 and PW-SH4. CE is in slot B. The dumped image is 30,721,008 bytes, `nk-c9100646`, SHA-256 `96af6ef1c3c04018b69bade9ff2882fc335147f8fd4e13fa5aac4be7c4f9d34b`. Its modules show it was built for the same Freescale i.MX28 BSP family as the PW-A7300's (`keybd_EDNA2.dll`, `edna2_powermgr.dll`).

Before this profile, the PW-SH2 U-Boot (`EDSA3EXE.BIN` copied from `EDSA2EXE.BIN`) booted a PW-SA3, but drew 800 pages on the 854-page panel and polled the SH2's I2C keyboard, which the PW-SH3 does not have, so the menu keys did nothing.

## U-Boot

`pwsh3_ce_defconfig` is `pwsh4_ce_defconfig` for the PW-SH3 board: `CONFIG_SHARP_PANEL_PAGES=854`, `CONFIG_SHARP_PANEL_FULL_FRAMES` and `CONFIG_SHARP_KEYBOARD_MATRIX`. `pwsh3.c` skips the early clock changes under `CONFIG_SHARP_WINCE` as `pwsh4.c` does. The U-Boot console is on the serial port only.

The keyboard is the same GPIO matrix as the PW-SH4's, with its keys in other places. The menu keys come from `keyboard_gpio` in Brainux's `imx28-pwsh3.dts`, where each entry is `<row column key>`: rows 0 to 4 are GPIO 4_0 to 4_4, row 5 is 4_6 and row 6 is 4_7, and columns 0 to 5 are GPIO 2_16 to 2_21 and column 6 is 4_8. Reading a key drives its column low and reads its row, as on the PW-SH4.

| Key | Column | Row |
| --- | --- | --- |
| Enter | 2_20 | 4_7 |
| Space | 2_18 | 4_6 |
| Up | 2_20 | 4_6 |
| Left | 2_19 | 4_6 |
| Down | 2_21 | 4_6 |
| Right | 4_8 | 4_6 |
| Back | 2_19 | 4_7 |

The PW-SH3's Enter, Up, Left and Right are on different positions from the PW-SH4's, so the PW-SH4 build's table does not work on it.

## Tested

On one PW-SA3: the boot menu (the arrows, Enter and Space), CE from a bundle made with `make-boot-nkp.mjs`, the Explorer desktop across the full panel width and touch at the right edge. After waking from idle the screen shows white for a moment, then recovers.

Not tested: Back and keys held at power-on, the first-boot generator (a release card with `nkgen.pak` for this image), Keymap, rotation, sound, Debug Agent and a PW-SH3.
