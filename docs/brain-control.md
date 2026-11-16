# Brain Control

`brain-control.cpl` is one Control Panel applet, **Brain**, for every setting brainsurgeon and its drivers add. A list of sections runs down the left, each with a Material Symbols icon; the chosen section's settings are on the right. Sections appear only on the models they apply to.

It is built as `brain-control.cpl`. The release card and every profile put it in `pwsh2-boot/Windows/`, so bstart copies it into `\Windows` at boot. Its settings files (`display.txt`, `sound.txt`, `power.txt`, `debug.txt`) are saved in `pwsh2-boot/PROFILE/`, so each model on a shared card keeps its own ([reference](reference.md#card-files)).

## Window

- A modal dialog filling the work area with no caption, so it cannot be dragged, laid out again when the screen rotates or the work area changes. Above the settings: the section's name in bold, a rule across the pane and a one-line description, which may wrap to two lines.
- Left: an owner-drawn list. Each row is the section's icon, drawn in the row's text colour, and its name. When the client area is narrower than 400 pixels (480x320 rotated, 320x480) the list shows icons only.
- Right: one child dialog per section, created once and shown when chosen. Page templates are at most 150 dialog units wide and 120 high, so they fit the A7300 main screen in either orientation.
- OK saves every section that changed and closes. Cancel undoes live previews (brightness, sub screen brightness, night light, touchpad, CPU mode) and closes.
- Icons are 24 pixels on screens with a short side under 480 pixels and 32 otherwise.

## Model Detection

When a recognised image gets its profile, `make-boot-nkp` and the patch maker add to the boot registry:

```
[HKEY_LOCAL_MACHINE\Software\Brain]
"Model"="PW-SH2"
"Profile"="pwsh2"
"Image"="CE V090, slot B"
[HKEY_LOCAL_MACHINE\Software\Brain\Modules]
"board.dll"="brain-drivers v0.1.5-alpha"
"ddraw_mx28.dll"="brain-display v0.1.2-alpha"
```

`Model` is the image's device (PW-SH2, PW-SB1, PW-SH4, PW-A7300 or PW-G5300). `Modules` names each module the profile takes from a release, with that release's repository and tag, read from the pinned input URL. The values are written once into `default.fdf`, so they are always present and match the drivers actually loaded. The board driver has no model information, and Sharp's OEM string has not been recorded on every model.

Without `Model` (an unrecognised image, `--no-profile`, or a `boot.nkp` made before this), Display, Touchpad and CPU show when their device answers (`BKL1:`, `PAD1:`, `BRD1:`), Night Light when the display driver reports escape 100155 through `QUERYESCSUPPORT` (brain-display does on the A7300), Sound, Power, Debug and About always, and Rotation not at all.

## Sections

| Section | Icon | PW-SH2 | PW-SB1 | PW-SH4 | PW-A7300, PW-G5300 | Settings |
| --- | --- | --- | --- | --- | --- | --- |
| Display | `brightness_6` | Yes | Yes | Yes | Yes | Main screen brightness (`BKL1:`, `brightness=` in `display.txt`): five levels on Sharp's driver, 1-100 when `BKL_IOCTL_GET_INFO` (0x100) answers `BKL2` with continuous levels, which also adds the dim level (`HKLM\Software\Brain\Backlight` `DimLevel`); sub screen brightness when `SLD1:` reports a backlight |
| Rotation | `screen_rotation` | Yes | Yes | | Yes | Orientation, use at startup, rotate in tablet mode and tablet orientation (PW-SH2 only), as `display.txt`; applied through `autorotate` when it runs |
| Night Light | `bedtime` | | | | Yes | `HKLM\Software\Brain\NightLight`. Moving Warmth previews it with escape 100155; the preview ends 2 s after the last change, when the section is left, and on OK or Cancel, and the driver's schedule decides the colour again |
| Touchpad | `touchpad_mouse` | | | | Yes | `HKLM\Software\Brain\Touchpad`, then `PAD1:` IOCTL 0x103 to reload |
| Sound | `volume_up` | Yes | Yes | Yes | Yes | System volume (`waveOutSetVolume`, `HKCU\ControlPanel\Volume` `Volume`), key clicks and screen taps (`Key`, `Screen`), then `AudioUpdateFromRegistry` so `waveapi.dll` plays them through the wave driver; bstart's watcher also applies them to Sharp's buzzer where there is one (not the PW-SH2) and saves `sound.txt`. Releasing the volume slider plays `\Windows\default.wav` at the new level |
| Power | `battery_full` | Yes | Yes | Yes | Yes | Battery level and voltage; idle times to dim, turn the screen off and sleep, on battery and plugged in, from 15 s to 30 min or Never, saved in `power.txt` and applied on OK ([idle timeouts](reference.md#idle-timeouts)); battery type (A7300 and G5300 only) in `power.txt`, sent to `BAT1:` at once |
| CPU | `speed` | Yes | Yes | Yes | Yes | `BRD1:` `BRD_IOCTL_CPU_SCALING`: Sharp's 240 MHz, always 454 MHz or follow load, saved with `BRD_CPU_SCALING_SAVE` (`HKLM\Software\Brain\CPU` `Scaling`); clock and switch count each second, and the busy percentage over that second from `GetIdleTime` (the driver's last 50 ms window where `GetIdleTime` is unsupported), and a graph of the last minute: load as bars, shaded while the CPU is at 454 MHz. The graph is a cached bitmap scrolled one column per sample, sized to the pane and up to 120 pixels high; sampling and drawing stop while the page is hidden |
| Debug | `bug_report` | Yes | Yes | Yes | Yes | **Start Debug Agent at boot** (`start-at-boot=` in `debug.txt`, saved on OK); **Start now** and **Stop now**, which act at once; whether Debug Agent is running and the transport it uses ([debug.txt](reference.md#debugtxt)). Running means its window (class `CeDebugManager`) exists; Stop now posts it Debug Agent's stop message, which switches USB back to mass storage |
| About | `info` | Yes | Yes | Yes | Yes | brainsurgeon version, model, profile, CE image, CE version, OEM string, screen size, board driver interface version and the `Modules` list |

The brainsurgeon version is `git describe --tags --always --dirty` at build time, passed to CMake as `BRAINSURGEON_VERSION` by `build.sh` and recorded in `build-info.txt`.

## CPU Governor

The board driver has one setting, `Scaling`, re-read every 5 s and settable at once through the IOCTL. Its governor constants are compiled in (brain-drivers' `cpu_scaling.c`).

The power scheme timeouts (`HKLM\System\CurrentControlSet\Control\Power\Timeouts`) are set from the Power section. bstart writes them again at every boot, so changes made in CE's own Power applet last until the next restart.

## Icons

`tools/make-icons.mjs` renders the section icons into `ce/brain-control/icons.h` and the applet icon into `ce/brain-control/brain-control.ico`. Both outputs are checked in; the build does not run the tool, and CI runs it with `--check` to confirm they match.

- Sources: Material Symbols Rounded, filled, 24 px SVGs from [google/material-design-icons](https://github.com/google/material-design-icons), in `ce/brain-control/icons/`. `ce/brain-control/icons/icons.json` pins the commit and each file's SHA-256. `--fetch` downloads them again from that commit. Apache License 2.0.
- Rasteriser: our own, in the tool. It parses the SVG path (lines, quadratic and cubic curves, arcs), flattens curves, and computes coverage with 16x16 samples per pixel, non-zero winding. The result is an 8-bit alpha mask per size, so the output is the same on every machine.
- At run time the applet blends each mask into a 32-bit DIB in the row's text colour over its background and copies it to the list.
- The applet icon is the `tune` symbol in white on a rounded blue square, 16 and 32 pixels, as 32-bit bitmap entries with a 1-bit mask. Control Panel gets it from `applet_icon()`.

Each applet returns its icon from `applet_icon()` (`ce/applet-icon.c`), which reads it from the applet's own file with `ExtractIconExW`. Control Panel loads applets one at a time, often at the same address, and CE caches icons from `LoadIconW` or `LoadImageW` by the address of the icon data. Our applets are small and laid out alike, so their icon data sits at the same address and every applet showed the first one's icon, whatever its resource IDs. CI fails if an applet calls `LoadIconW` or `LoadImageW` for an icon.

## Retired Applets

Brain Control replaced `rotate.cpl`, `brightness.cpl`, `nightlight.cpl` and `touchpad.cpl`. It uses the same files, registry values and driver interfaces, so nothing needed migrating. bstart deletes the old copies from the card at boot (`pwsh2-boot/rotate.cpl` and `pwsh2-boot/Windows/{brightness,nightlight,touchpad}.cpl`). `\Windows` is in RAM, so nothing else holds them after a restart. `autorotate.exe` stays: it applies the startup rotation and follows tablet mode. `CPLBRAIN.CPL` (CE's own Date/Time, Power, Stylus, Volume and others on the A7300) stays.
