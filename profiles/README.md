# Profiles

`profiles.json` lists, for each Sharp Brain CE image recognised by SHA-256, the drivers, registry additions, word patches and card files `make-boot-nkp` and the patch maker add. It is read by `tools/lib/profiles.mjs`.

File paths are relative to the repository, release or web page root. `build/` is the build output (`out/` from `sh build.sh out`, or `--build`). `inputs/` holds the release assets pinned in `inputs`, fetched and checked by `node tools/fetch-inputs.mjs`. `binaries/` holds files shipped as built ([binaries](../binaries/README.md)).

A module entry with `file` replaces or adds a ROM module. One with `rom` is made from the image itself: the module is extracted as a relocatable DLL, then imports are redirected (`redirectImports`) and bytes replaced (`patchBytes`, file offsets in the extracted DLL, each original checked). `cardFiles` go on the card at `path`. Files only one profile uses go in `pwsh2-boot/PROFILE/`, with files for `\Windows` in `pwsh2-boot/PROFILE/Windows/`, so they never reach another model; bstart copies only its own profile's folder. `sharedCardFiles` are card files every profile gets, listed once. Card files with `file` also go on the release card.

Every profile also adds `HKLM\Software\Brain` `Model` (the image's device), `Profile` and `Image` (its description), and under `Software\Brain\Modules` each module taken from a pinned release with that release's repository and tag. [Brain Control](../docs/brain-control.md#model-detection) reads them.

| File | Origin |
| --- | --- |
| `registry/board.reg` | [brain-drivers](https://github.com/grimace-sdk/brain-drivers) `v0.1.5-alpha` `src/board/board.reg`, SHA-256 `983427c91a3636fddee3590e54071964daee7b058dbec1caaea466c6ef04e01e` |
| `registry/touchpad.reg` | brain-drivers `v0.1.5-alpha` `src/touchpad/touchpad.reg`, SHA-256 `65f0ede57b41d995e28bf3d40a778d7bf4b129e73e362c0afa2340c279ae8c27` |
| `registry/touch-areas-pwsh4.reg` | Moves the PW-SH4's touch soft-key areas past the 848-pixel desktop |
| `registry/crash-handler.reg` | Registers `pwsh2-boot\crashlog.exe` as the JIT debugger in the boot registry ([launch.txt](../docs/windows-ce.md#4-choose-what-to-start), `crash-handler`) |
| `startup/keymap.lnk` | Starts `pwsh2-boot\Keymap.exe` ([Keymap](../ce/keymap/README.md)) from `\Windows\StartUp` |
| `startup/keyboard-help.lnk` | Start menu Keyboard Help: `pwsh2-boot\Keymap.exe help` |
| `patches/ymu818-volume-pwsh2.json`, `patches/ymu818-volume-pwsb1.json` | Make Sharp's `ymu818_wavdev.dll` follow the system volume |
| `patches/sx8650-width-pwsh4.json` | Makes Sharp's PW-SH4 `sx8650_touchscreen.dll` clamp touch X to 847, for the 848-pixel desktop |
