# Models

One card works in every Brain below, and you can move it between them: each model starts its own U-Boot and keeps its own patches and settings.

**PW-SH2**
- U-Boot `EDSA2EXE.BIN`, profile `pwsh2`.
- Tested on one PW-SH2 with CE V090, from a fresh card to the Explorer desktop, with suspend and resume.

**PW-SB1**
- U-Boot `EDSA1EXE.BIN`, profile `pwsb1`.
- U-Boot tested. The profile and volume patch have not been booted.

**PW-SH3** and **PW-SA3**
- U-Boot `EDSA3EXE.BIN`, profile `pwsh3`. Details in [PW-SH3 and PW-SA3](pwsh3.md).
- Tested on one PW-SA3: boot menu keys, patched boot, the Explorer desktop across the panel and touch.

**PW-SH4**
- U-Boot `EDSH4EXE.BIN`, profile `pwsh4`. Details in [PW-SH4](pwsh4.md).
- Tested on CE V100: boot menu, patched boot, display, backlight, touch and Explorer.

**PW-A7300** and **PW-G5300**
- U-Boot `EDNA3EXE.BIN`, profile `pwa7300`. Details in [PW-A7300](a7300.md).
- Tested: patched boot and the Explorer desktop.

**PW-SH1**, **PW-HC4** and **PW-SJ1**
- U-Boot `EDSA1EXE.BIN`, no profile: bstart only, with Sharp's drivers. Untested.

**PW-A7400**
- Unsupported. Its EBOOT also runs `EDNA3EXE.BIN`, which is built for the PW-A7300. Remove `EDNA3EXE.BIN` from a card used in a PW-A7400.

**PW-SH5, PW-SH6, PW-SH7**
- No CE U-Boot. u-boot-brain has Linux configurations for them (`EDSA5EXE.BIN`, `EDSA6EXE.BIN`, `EDSA7EXE.BIN`).

**PW-A5300, PW-A7200 and other models**
- No U-Boot.

## Adding a Model

A profile needs the model's CE image (`nk-slot-*.bin`, from `dump-nk`) to be identified by SHA-256 and checked against. A CE U-Boot for a model also needs its EBOOT, for the platform's file names, config format and the BSP arguments address U-Boot hands to CE.
