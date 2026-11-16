# Binaries

Shipped as built, from a patched CE 6 build linked from Microsoft Platform Builder libraries.

| File | Bytes | SHA-256 | Use |
| --- | --- | --- | --- |
| `explorer.exe` | 288768 | `11bd4ac4259f8c7214ebebe2ad84ba61920a5454f979ad87ac2eaff758ae8f72` | The desktop on every model, from the card (`pwsh2-boot/PROFILE/explorer.exe`), with Restart in the Start menu and Brain in Start > Settings |
| `pwa7300/gwes.dll` | 766464 | `292c5b647b79f6eba532f95ce6d9b01bfc0df05299801d886b594437228f1593` | Replaces the PW-A7300's `gwes.dll`; draws a mouse cursor |
| `pwa7300/ceshell.dll` | 292864 | `c81ecdadcfbf50763609a6ac99606a9996f98633a338fe73a297ac416c7db330` | Replaces the PW-A7300's `ceshell.dll`, for `explorer.exe` |
