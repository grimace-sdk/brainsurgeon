# Windows CE and Linux

One card that boots either Sharp's Windows CE, set up as in [Windows CE only](windows-ce.md), or [Brainux](https://github.com/brain-hackers/buildbrain), a Debian-based Linux for Brain devices.

You need everything listed in [Windows CE only](windows-ce.md), plus a Brainux SD card image. A Brainux image has a small FAT32 boot partition (kernel, device trees, `uEnv.txt`) and a Linux root partition.

## 1. Write Brainux to the Card

Download `sdimage-<date>.zip` from the [buildbrain releases](https://github.com/brain-hackers/buildbrain/releases) (not the `x1` image) and write it to the card with [balenaEtcher](https://etcher.balena.io), Raspberry Pi Imager or `dd`. This erases the card.

Skip this step if the card already has Brainux on it.

## 2. Add the Boot Menu

Copy the zip's `card/` folder to the root of the card's FAT32 partition, alongside Brainux's files. Replace Brainux's own U-Boot files, such as `edsa2exe.bin`, when asked. Then create `pwsh2-boot/menu.txt` containing:

```
linux=yes
```

On the PW-SH2:

```
EDSA2CFG.BIN
EDSA2EXE.BIN
pwsh2-boot/nkgen.bin
pwsh2-boot/nkgen.pak
pwsh2-boot/menu.txt
uEnv.txt               from Brainux
zImage                 from Brainux
imx28-pwsh2.dtb        from Brainux
```

Choosing **Linux** in the menu runs u-boot-brain's usual Linux boot, which reads `uEnv.txt` and loads the kernel and device tree it names. The kernel and device tree can be moved, for example into a `brainux/` folder, with `uEnv.txt`'s `image=` and `fdt_file=` lines changed to match. The Linux entry appears only when the zImage `uEnv.txt` names is on the card. [menu.txt](reference.md#menutxt) can also boot another kernel, initrd or boot script, from any partition, and make Linux the default.

## 3. Set Up Windows CE

Follow steps 2 and 4 of [Windows CE only](windows-ce.md): boot each Brain once to make its patches, and choose what to start.

The Brainux boot partition is small. The release card files take about 3.5 MB, and each Brain's patches up to 2 MB more. The command-line route needs about 30 MB more for the CE image copy while you make `boot.nkp`. The program you start from CE also needs room. To make more space, grow the FAT32 partition and move or grow the Linux root partition with a partitioning tool before setting up CE.

## Using It

The menu appears over the Brain splash screen when the Brain is turned on with the card in:

| Key | Action |
| --- | --- |
| Left, Right, Up, Down | Choose Windows CE, CE stock or Linux |
| Enter or Space | Start the selection |
| Back | Show the U-Boot entry |
| Any of these | Stop the 10 second countdown |

With no key pressed, Windows CE starts after 10 seconds. The keys on each model are in [Menu and U-Boot Commands](reference.md#menu-and-u-boot-commands).

To boot Sharp's software as stock, remove the card or delete the Brain's U-Boot file (`EDSA2EXE.BIN` on the PW-SH2).
