# Windows CE Only

Boot Sharp's Windows CE straight into a program of your choice, such as a desktop shell, instead of the dictionary. One card serves every supported Brain: copy the release's card files once, then boot each Brain from it.

You need:

- A Sharp Brain with a U-Boot in the release: PW-SH2, the ED-SA1 models (PW-SB1, PW-SH1, PW-HC4, PW-SJ1), PW-SH4, PW-A7300 or PW-G5300. See [Models](models.md) for which are tested.
- A microSD card with a FAT32 first partition. It can be the card you already use.
- A computer with a card reader.
- The latest [release](https://github.com/grimace-sdk/brainsurgeon/releases) zip. Check its files against `SHA256SUMS`.

## 1. Copy the Card Files

Copy everything in the zip's `card/` folder to the root of the card. It includes:

```
EDSA2CFG.BIN, EDSA2EXE.BIN          PW-SH2
EDSA1CFG.BIN, EDSA1EXE.BIN          PW-SB1, PW-SH1, PW-HC4, PW-SJ1
EDSH4EXE.BIN                        PW-SH4
EDNA3CFG.BIN, EDNA3EXE.BIN          PW-A7300 and PW-G5300
pwsh2-boot/nkgen.bin                the patch generator
pwsh2-boot/nkgen.pak                its drivers, profiles and patches
pwsh2-boot/autorotate.exe
pwsh2-boot/crashlog.exe
pwsh2-boot/Keymap.exe
pwsh2-boot/Windows/brain-control.cpl
pwsh2-boot/debug/
pwsh2-boot/pwsh2/launch.txt
pwsh2-boot/pwsb1/launch.txt
pwsh2-boot/pwsh4/launch.txt
pwsh2-boot/pwa7300/launch.txt
```

Each Brain's bootloader (EBOOT) reads only its own model's files, so the others are ignored. Leave anything already on the card where it is. Never put a Sharp `.SDU` update file on the card.

## 2. Boot Each Brain

Put the card in the Brain and turn it on. A menu appears over the Brain splash screen and Windows CE starts after 10 seconds. Choose **CE stock** to boot Sharp's dictionary unpatched ([keys](reference.md#menu-and-u-boot-commands)).

The first Windows CE boot of each Brain from the card makes that Brain's patches from its own CE image. The progress bar shows the stages: `First start: reading Windows CE`, then `Identifying image`, `Extracting modules` (PW-A7300 and PW-G5300), `Merging registry` and `Building patches`, then `Saving to card` and `Patching`. U-Boot writes:

- `pwsh2-boot/nk-<id>/`: the patch bundle `boot.nkp`, the drivers it loads in `modules/`, `cache.txt` and `nkgen.log`. `<id>` is the image identity, a CRC-32 of the image's ROM tables in 8 hex digits, so each CE image has its own folder.
- `pwsh2-boot/PROFILE/`: files for that model only, such as its `explorer.exe`.

Later boots use these files and start sooner. A newer release's `nkgen.bin` or `nkgen.pak` makes each Brain generate again on its next boot. To make one Brain's patches again, delete its `nk-<id>` folder.

Brains listed in [profiles/profiles.json](../profiles/README.md) get their drivers, registry additions and patches. Any other image gets a bundle that only adds bstart, with no drivers: CE starts the program in `launch.txt` with Sharp's own drivers. Brain Control's About page shows what was patched ([boot status](reference.md#boot-status)).

`boot.nkp` contains:

| Patch | Effect |
| --- | --- |
| `skip` lines | U-Boot does not read Sharp's dictionary, handwriting, speech and update modules, which nothing else uses: about half of the image |
| `bstart` | Adds `bstart.exe` to the image and starts it instead of Sharp's shell |
| `module NAME` | Recognised Brains: loads a driver from `modules/` beside `boot.nkp` in place of Sharp's, or as a new one |
| `words NAME` | PW-SH2 and PW-SB1: makes Sharp's sound driver follow the system volume |

Skipping is on for every image: the skip list is by module name, and recognised Brains need it, since their drivers load into the skipped space. Only names present in the image are skipped. For an image the skip list was not checked against, the `skip` step in `nkgen.log` says so; if CE then misbehaves, make the bundle with the command line and `--no-skip` ([below](#3-command-line)).

## 3. Command Line

For images or set-ups the generator can't handle, or for the development options, make the bundle on a computer from a copy of the CE image.

Create an empty `pwsh2-boot/dump-nk` on the card and boot the Brain. U-Boot copies the CE image it is about to boot to `pwsh2-boot/nk-slot-A.bin` or `nk-slot-B.bin` (about 30 MB), and deletes `dump-nk`. The boot pauses while it writes. Two Brains can use the same slot name (the PW-SH2 and PW-SH4 boot slot B, the PW-A7300 and PW-G5300 slot A), so move one Brain's copy off the card before copying the next.

The image is Sharp's software. Keep it for yourself; do not share it.

With [Node.js](https://nodejs.org) 20 or later, from the unzipped release:

```sh
node tools/make-boot-nkp.mjs --image /path/to/card/pwsh2-boot/nk-slot-B.bin --folder crc --card /path/to/card
```

`--folder crc` writes `pwsh2-boot/nk-<id>/`, which U-Boot uses as it stands, so the generator leaves that image alone. Delete the folder to go back to the generator. `--help` lists the other options.

You can then delete `nk-slot-*.bin` from the card, or keep it to make `boot.nkp` again later.

Make every Brain's bundle with the same release as the card. bstart comes from the bundle, and a newer bstart moves files an older one still expects (see [Files for \Windows](#files-for-windows)).

## 4. Choose What to Start

bstart reads the Brain's profile from `HKLM\Software\Brain` `Profile` (`Model` with the punctuation removed if there is no `Profile`): `pwsh2`, `pwsb1`, `pwsh4` or `pwa7300`, the last for the PW-G5300 too. It reads `pwsh2-boot/PROFILE/launch.txt`, and `pwsh2-boot/launch.txt` if that is missing or the Brain has no profile.

The release card has a `launch.txt` in each profile folder that starts Explorer from the same folder. A Brain without a profile needs a `launch.txt` of your own. Write it as UTF-8 or plain ASCII text:

```
program=\Storage Card\MyApp\MyApp.exe
arguments=
wait-for-window=
restart-keyboard=yes
restart-shortcut=yes
wallpaper=
```

Copying a newer release card over an older one replaces the default launch files. Keep a copy of yours if you changed them.

- `program`: full CE path. The card is `\Storage Card`.
- `arguments`: optional command line.
- `wait-for-window`: optional window class. bstart shows a blue screen until a window of this class appears, for up to 20 seconds. Leave it empty to remove the blue screen as soon as the program has a window in the foreground.
- `restart-keyboard`: `yes` (the default) or `no`. Sharp's soft keyboard driver holds a stale window from earlier in boot, which can hang a desktop shell such as `explorer.exe` on about half of all boots. bstart restarts the driver (`SIP0:`) before starting the program to prevent this. Set `no` only if your program does this itself.
- `restart-shortcut`: `yes` (the default) or `no`. Adds **Restart** to the top of Start > Programs. It restarts the Brain back to the boot menu. CE's own **Suspend** already does what the power key does, so there is no separate shut down.
- `crash-handler`: optional full path of a JIT debugger to register for this session, such as `\Storage Card\pwsh2-boot\crashlog.exe`. Without it, Sharp's own handler (`MyJITDebugger.exe`) shows a full-screen image and waits for a reset whenever any program crashes. `crashlog.exe` instead writes the fault, registers and loaded modules to `pwsh2-boot\crash.log` and ends only the crashed program. On the PW-SH2 this setting has no effect: the kernel keeps the JIT debugger it read during boot, before bstart runs, and faults still go to Sharp's handler with the registry naming `crashlog.exe`. Recognised Brains other than the PW-SH4 get `HKLM\Debug\JITDebugger` set in the boot registry instead (`profiles/registry/crash-handler.reg`).
- `wallpaper`: empty (the default) for a plain blue desktop, `stock` to keep CE's own wallpaper, or the full path of an image. The plain desktop avoids burning a static logo into the panel.
- `startup`: optional full path of a program to run first, such as a registry restore. bstart starts it before copying `pwsh2-boot/Windows` and waits up to 10 seconds for it to set the named event `pwsh2-boot/StartupReady` before starting `program`.

Without a launch file, or if the program is missing or will not start, bstart stays on its blue screen and says what is wrong. It does not fall back to Sharp's shell: that shell starts the dictionary, which needs modules U-Boot may have skipped. To use the dictionary, start the Brain without the card.

Before starting the program, bstart also does what Sharp's dictionary supervisor would have done for power management: auto power-off and the power key keep working.

### Files for \Windows

Everything in `pwsh2-boot/Windows` on the card, subfolders included, is copied into `\Windows` at boot, then everything in `pwsh2-boot/PROFILE/Windows`, before the crash handler is registered and the program starts. Use the first for files every model needs and the second for one model's files, such as the PW-A7300's Control Panel (`CPLBRAIN.CPL`). `\Windows` is in RAM, so nothing persists past a reset.

A file in `pwsh2-boot/PROFILE/Windows` replaces one of the same name in `pwsh2-boot/Windows`: bstart deletes the shared copy from the card. Earlier releases put the PW-A7300's `CPLBRAIN.CPL`, `braincrypt.dll`, `brainras.dll` and `brainsip.dll` in the shared folder, where the other models copied them too.

### Brightness and Key Clicks

Brain Control's Display section sets the backlight with `BKL1:` and saves `brightness=` in `pwsh2-boot/PROFILE/display.txt`; `bstart` applies it at boot.

Sharp's buzzer clicks follow CE's own Volume & Sounds settings: "Key clicks" and "Screen taps" (`HKCU\ControlPanel\Volume` `Key` and `Screen`). After starting the program, `bstart` stays running and waits for changes to that key (`CeFindFirstRegChange`). It turns each sound on or off by adding or removing `Drivers\BuiltIn\Buzzer\Event\Click` (keyboard) and `Event\Touch` (screen) and signalling `Buzzer_Event_Reset`, which makes Sharp's buzzer driver reload its event list. The choice is saved as `key-clicks=` and `screen-taps=` in `pwsh2-boot/PROFILE/sound.txt` and applied at boot. The PW-SH2 has no buzzer driver (its registry names `EDNA2_Buzzer.dll`, which is not in the ROM); its clicks come from CE's `waveapi.dll` through the speaker, which reads the same values when told to with `AudioUpdateFromRegistry`. bstart does this at boot. Without `sound.txt`, both are off. Soft and loud are the same; the buzzer has no volume control.

### Start Menu

Shortcuts in `pwsh2-boot/Programs` on the card are copied into Start > Programs at boot. Subfolders become program groups. A shortcut is a CE `.lnk` file: the length of the target, `#`, then the target, quoted if it has spaces:

```
33#"\Storage Card\Apps\SHAM-7X0.exe"
```

### Programs to Start

Sharp's ROM has no usable desktop shell, so you supply one. Options:

- **Explorer**, `explorer.exe` in each profile folder: CE 6.0's desktop and Start menu, with Restart in the Start menu and Brain in Start > Settings.
- **[ceOpener_mod](https://ho.ie-t.net/it/brain/ceopener_mod/)**, a launcher and desktop for Brain devices: `program=\Storage Card\アプリ\ceOpener_mod\AppMain.exe`, wherever you put it. Not tested with bstart. It expects the dictionary to be running and may warn that it is not.
- **Your own program.** Anything built for CE 6.0 ARMv4I that only uses DLLs in Sharp's ROM, or DLLs you put next to it.

## Screen Rotation and Brightness

Open **Settings > Control Panel > Brain** ([Brain Control](brain-control.md)). Its Rotation section chooses the screen orientation and whether the screen rotates in tablet mode; its Display section sets the backlight brightness. The Brain always starts in landscape unless you tick **Use this orientation at startup**. Settings are saved to `pwsh2-boot\PROFILE\display.txt` on the card, so each model keeps its own.

Startup rotation and tablet mode need `pwsh2-boot\autorotate.exe` on the card. It applies the saved rotation at boot and stays running to follow tablet mode. Delete it to turn rotation at startup and in tablet mode off.

bstart deletes the separate applets earlier releases put on the card (`pwsh2-boot/rotate.cpl` and `brightness.cpl`, `nightlight.cpl` and `touchpad.cpl` in `pwsh2-boot/Windows`).

Most apps follow a rotation. Some, such as Pocket Paint, do not handle being resized and are best restarted after rotating.

## Undo

Delete the Brain's U-Boot file (`EDSA2EXE.BIN`, `EDSA1EXE.BIN`, `EDSH4EXE.BIN` or `EDNA3EXE.BIN`) from the card, or remove the card. The Brain then boots exactly as stock. To boot Sharp's dictionary once, choose **CE stock** in the menu. To keep the menu but boot CE unpatched every time, create an empty `pwsh2-boot/no-nkgen` and delete `pwsh2-boot/boot.nkp` and the `pwsh2-boot/nk-*` folders.

## Problems

- **No menu or U-Boot does not start:** check the Brain's files are in the card's root (`EDSA2CFG.BIN` and `EDSA2EXE.BIN` on the PW-SH2) and the first partition is FAT32.
- **First start fails, or CE starts without the patches:** `pwsh2-boot/boot-status.txt` has the error and `pwsh2-boot/nk-<id>/nkgen.log` the generator's log ([boot status](reference.md#boot-status)).
- **CE does not start or stops:** create an empty `pwsh2-boot/log` file and boot again. U-Boot writes `pwsh2-boot/uboot.log`, which lists each patch as applied or skipped and why. See [reference](reference.md#diagnostics).
