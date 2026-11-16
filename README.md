# brainsurgeon

Turn a cheap Japanese Sharp Brain Dictionary into an English Windows CE Handheld with nothing but a microSD card. Pull the card and it returns to stock.

One card serves every supported model ([Models](docs/models.md)). Its boot menu offers:

- **Windows CE**: a desktop instead of the dictionary, with faster open-source display, board, touch and sub screen drivers, rotation, a Control Panel and full-screen app support.
- **CE stock**: Sharp's dictionary, unpatched.
- **Linux**: [Brainux](https://github.com/brain-hackers/buildbrain) or another kernel on the card, when [menu.txt](docs/reference.md#menutxt) turns it on.
- **U-Boot**: the U-Boot console, when `menu.txt` turns it on.

Doesn't permanently modify your device or write to internal storage: Sharp's CE is patched in RAM at each boot. Remove the card, or delete the Brain's U-Boot file (`EDSA2EXE.BIN` on the PW-SH2) from it, and the Brain boots exactly as before.

Build apps for it with [grimace](https://github.com/grimace-sdk/grimace), the Windows CE SDK for modern compilers.

## Features

- **Display driver** ([brain-display](https://github.com/grimace-sdk/brain-display)), all models: faster than Sharp's on all 22 drawbench cases on the PW-SH2, such as full-screen fill 2.9x, ClearType text 1.5x, stretched blits 17.7x and sprite masks 29.9x. Tear-free refresh, paced to the panel.
- **Rotation**, PW-SH2 and PW-A7300: all four orientations, rotated by the PXP, with startup rotation and automatic rotation in PW-SH2 tablet mode.
- **Full-screen apps**, all models: present mode runs 800x480 apps at 51.7 fps on the PW-SH2 and smaller buffers at the full 59.3 Hz, with up to 8 hardware overlays. An SDL 3 test app runs at 60 fps.
- **Sleep**, PW-SH2 and PW-A7300: resume from deep sleep restores the LCD controller, pins and panel.
- **Backlight** ([brain-drivers](https://github.com/grimace-sdk/brain-drivers)), PW-SH2, PW-SH4 and PW-A7300: 100 brightness levels on a perceptual curve, down to 0.36% of full, plus a dim level for idle. Sharp's driver has five.
- **Idle dimming and sleep**, all models: dim, screen off and sleep times for battery and mains, set in Brain Control.
- **CPU scaling** ([brain-drivers](https://github.com/grimace-sdk/brain-drivers)), all models: 240 MHz when idle, 454 MHz under load. Sharp's CE stays at 240 MHz.
- **Keyboard**, all models: [Keymap](ce/keymap/README.md) gives key remaps, symbol and number layers, volume keys and a keyboard map. On the PW-A7300, Shift and Fn on 機能 and 音声, the current layer on the sub screen and Fn+arrow brightness.
- **Touchpad mouse**, PW-A7300 and PW-G5300: the touch panel under the sub screen works as a mouse, with tap to click, acceleration and five speeds.
- **Sub screen**, PW-A7300: 4-level greyscale on the 240x120 sub screen, adjustable backlight and pitched buzzer tones.
- **Night light**, PW-A7300 and PW-G5300: warms the panel through its gamma on a schedule.
- **Desktop**, all models: Explorer and Control Panel.
- **Settings**, all models: [Brain Control](docs/brain-control.md), one Control Panel applet for brightness, rotation, night light, touchpad, sound, power, battery, CPU and Debug Agent, showing what applies to each model.
- **Boot menu**, all models: Windows CE, Sharp's stock CE and, when set up, Linux and the U-Boot console, with a progress bar while CE loads ([menu.txt](docs/reference.md)).
- **Debug Agent** ([ce-debug](https://github.com/grimace-sdk/ce-debug)), all models: copy files, run programs, set the clock and take screenshots from a computer over USB. Off by default; turn it on in Brain Control ([debug.txt](docs/reference.md#debugtxt)).
- **Start-up**, all models: a program of your choice from each model's `launch.txt`, crash logging and the battery type setting.
- **Volume fix**, PW-SH2 and PW-SB1: corrects Sharp's audio driver volume control.

## Supported Devices

**PW-SH2** ([search Buyee](https://buyee.jp/item/crosssearch/query/PW-SH2?price_max=4000&sort=new&fc=6afb06460b5eb))
- Windows CE at 800x480 on a 480x800 panel, in any rotation, with automatic rotation in tablet mode.
- Touchscreen and 100 backlight levels.
- Keymap with symbol and number layers, key remaps and the keyboard map.

**PW-SB1** ([search Buyee](https://buyee.jp/item/crosssearch/query/PW-SB1?price_max=4000&sort=new&fc=6afb06460b5eb))
- As the PW-SH2, with Sharp's five-level backlight.

**PW-SH4** ([search Buyee](https://buyee.jp/item/crosssearch/query/PW-SH4?price_max=4000&sort=new&fc=6afb06460b5eb))
- Windows CE at 848x480 on a 480x854 panel.
- Touchscreen, 100 backlight levels, and Keymap with the PW-SH2's layers and a keyboard map.

**PW-A7300** ([search Buyee](https://buyee.jp/item/crosssearch/query/PW-A7300?price_max=4000&sort=new&fc=6afb06460b5eb))
- Windows CE at 480x320, with rotation.
- A 240x120 sub screen with 4-level greyscale, its own backlight and buzzer tones.
- The touch panel under the sub screen is a mouse with a drawn cursor and tap to click.
- 100 backlight levels, night light through the panel's gamma, and Keymap with Shift and Fn.

**PW-G5300** ([search Buyee](https://buyee.jp/item/crosssearch/query/PW-G5300?price_max=4000&sort=new&fc=6afb06460b5eb))
- As the PW-A7300.

The Buyee links search for listings under 4,000 yen, newest first.

## Set-up

Copy the release's `card/` folder to the card, then boot each Brain from it. On its first boot from the card, a Brain shows "First start" stages while U-Boot makes its patches from its own CE image and saves them to `pwsh2-boot/nk-<id>/`. Later boots use them.

- [Windows CE only](docs/windows-ce.md)
- [Windows CE and Linux](docs/dual-boot.md)
- [Reference](docs/reference.md): card files, menu, `launch.txt`, `boot.nkp`, U-Boot commands, diagnostics.

For images or set-ups the generator can't handle, `tools/make-boot-nkp.mjs` makes the same files on a computer from a copy of the CE image ([Command Line](docs/windows-ce.md#3-command-line)). Releases contain the card files, `make-boot-nkp.mjs`, which needs [Node.js](https://nodejs.org) 20 or later, and everything the profiles add: `build/` (bstart and the CE programs), `inputs/` (pinned brain-display and brain-drivers release files), `binaries/` and `profiles/`.

## Building

`build.sh` builds one model's U-Boot and the CE programs in a pinned Debian container. `BRAIN_MODEL` picks the model: `pwsh2` (the default, `edsa2exe.bin`), `pwa7300` (`edna3exe.bin`) or `pwsh4` (`edsh4exe.bin`). Run it on Linux with Docker:

```sh
sh build.sh out
```

`out/build-info.txt` records every input and the output hashes. CI builds each model twice on separate runners, checks the results match, fetches the profile inputs (`node tools/fetch-inputs.mjs`, which checks each SHA-256), and attaches a release zip made by `tools/package.sh` with every model's U-Boot, `nkgen.bin` and `nkgen.pak` (made by `tools/make-nkgen-pak.mjs`).

From a checkout, after `sh build.sh out` and `node tools/fetch-inputs.mjs`:

```sh
node tools/make-boot-nkp.mjs --image nk-slot-B.bin --build out --card /path/to/card
```

- **U-Boot**: [grimace-sdk/u-boot-brain](https://github.com/grimace-sdk/u-boot-brain), branch `pwsh2-boot`, commit pinned in `build.sh`. Loader in `board/sharp/common/wince/`, devices in `configs/*_ce_defconfig`.
- **Debug Agent**: [grimace-sdk/ce-debug](https://github.com/grimace-sdk/ce-debug), commit pinned in `build.sh`: `debugmgr.exe` and `usbserial.dll`.
- `uboot/tests/`: host tests for U-Boot's loader: the `boot.nkp` parser, progress bar, menu, and the generator lookup, cache and status with a fake card. `sh uboot/tests/run.sh` fetches the pinned U-Boot; set `UBOOT_SOURCE` to use a local tree, and `NK_IMAGES` with `NKGEN_BUILD` to run the flow on real images.
- `ce/`: Windows CE programs, built with [grimace](https://github.com/grimace-sdk/grimace) and [llvm-builds](https://github.com/grimace-sdk/llvm-builds): `bstart.exe` (the launcher), `autorotate.exe` (startup rotation and tablet mode), `brain-control.cpl` ([Brain Control](docs/brain-control.md), one applet for every setting), the A7300 Control Panel stand-ins (`braincrypt.dll`, `brainras.dll`, `brainsip.dll`) and `keymap.exe` ([Keymap](ce/keymap/README.md), key remaps and the keyboard map). The board, backlight, touch and sub screen drivers come from [brain-drivers](https://github.com/grimace-sdk/brain-drivers).
- `ce/drivers/ddi-trace/`: display diagnostics. `ddi-trace.dll` logs display driver calls, and the test programs built with it draw test patterns and save logs and screenshots to the card.
- `tools/bmp-diff.py`, `tools/bmp-contact-sheet.py`: display diagnostics. Compare two screenshots, or lay screenshots out on one sheet.
- `tools/lib/`: CE ROM image, registry, LZX and `boot.nkp` code, shared by the command-line tools.
- `tools/make-boot-nkp.mjs`: makes `boot.nkp` and the card files from a CE image.
- `tools/make-nkgen-pak.mjs`: makes `nkgen.pak`, the inputs for the [on-device generator](docs/on-device-generation.md), from the profiles; `--describe` prints one.
- `nkgen/`: the [on-device generator](docs/on-device-generation.md), `nkgen.bin` for U-Boot and `nkgen-host`, made by `nkgen/build.sh`. `tools/check-nkgen.sh` compares it with `make-boot-nkp.mjs`.
- `profiles/`: per-device drivers, registry additions and patches.
- `binaries/`: `explorer.exe` for every model and the A7300 `gwes.dll` and `ceshell.dll`, shipped as built.
- `tools/make-edsa2cfg.mjs`: makes `EDSA2CFG.BIN`, `EDSA1CFG.BIN` and `EDNA3CFG.BIN`.
- `tools/nk/`: CE ROM image tools: `list-nk.mjs` lists modules and files, `dump-registry.mjs` prints the ROM registry, `rom-layout-svg.mjs` draws the layout after `boot.nkp`.

Model notes: [Models](docs/models.md), [PW-SH4](docs/pwsh4.md), [PW-A7300](docs/a7300.md).

## How It Works

1. Sharp's bootloader (EBOOT) finds its platform's config file on the card, `EDSA2CFG.BIN` on the PW-SH2, which tells it to run `EDSA2EXE.BIN` from the card. Each model looks for its own names, and the PW-SH4 needs no config file ([EBOOT files](docs/reference.md#eboot-files)).
2. `EDSA2EXE.BIN` is U-Boot. It draws the menu over the Brain splash screen. Windows CE starts after 10 seconds.
3. For Windows CE, U-Boot reads Sharp's CE image from internal storage into RAM and applies the image's `boot.nkp` from the card. Its sections, each checked against the image before anything is changed:
   - **Skips**: modules only the dictionary uses are not read, which speeds up loading and frees space in the image.
   - **Launcher**: `bstart.exe` is added to the ROM file table and takes `HKLM\init\Launch50`, where Sharp's `explorer.exe` (which starts the dictionary) was.
   - **Modules**: our drivers and DLLs are loaded from the card into the skipped space, and the ROM modules they replace are renamed so CE loads ours ([image layout](docs/reference.md#image-layout)).
   - **Registry**: keys for the new drivers are merged into a copy of the boot registry (`default.fdf`).
   - **Patches**: checked word and code patches to Sharp's modules, such as the volume fix and, on the A7300, the display hook.
4. CE starts. bstart prepares the power manager and its idle timeouts, backlight, battery type and rotation, copies `pwsh2-boot/Windows` and the model's own `pwsh2-boot/PROFILE/Windows` into `\Windows`, then starts the program named in `pwsh2-boot/PROFILE/launch.txt`, or `pwsh2-boot/launch.txt`, and Debug Agent if `debug.txt` asks for it.

`boot.nkp` is made from each Brain's own CE image: Sharp shipped more than one CE version. On a Brain's first boot from the card, U-Boot runs the [on-device generator](docs/on-device-generation.md) (`pwsh2-boot/nkgen.bin`) on the image in RAM with the release's drivers and profiles (`nkgen.pak`), and saves `boot.nkp` and its files in `pwsh2-boot/nk-<id>/`. `tools/make-boot-nkp.mjs` makes the same files from a copy of the image. U-Boot checks every patch against the image, so a `boot.nkp` for a different image changes nothing.

All three recognise the PW-SH2, PW-SB1, PW-SH4, PW-A7300 and PW-G5300 images listed in [profiles/profiles.json](profiles/README.md) and add that Brain's drivers: [brain-display](https://github.com/grimace-sdk/brain-display), [brain-drivers](https://github.com/grimace-sdk/brain-drivers), registry additions and the Explorer desktop. The A7300 also gets its display patch, keyboard, sub screen driver and Control Panel.

## Licence

[MIT](LICENSE), except:

- The U-Boot builds (`EDSA2EXE.BIN`, `EDSA1EXE.BIN`, `EDSH4EXE.BIN`, `EDNA3EXE.BIN`): GPL-2.0+, as [U-Boot](https://github.com/brain-hackers/u-boot-brain). Its complete source is the [grimace-sdk/u-boot-brain](https://github.com/grimace-sdk/u-boot-brain) commit pinned in `build.sh`.
- The menu's Tux icon in the U-Boot build: the Linux kernel's Tux logo, by Larry Ewing (lewing@isc.tamu.edu) with The GIMP.
