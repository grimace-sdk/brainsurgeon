# Binaries

Shipped as built. `explorer.exe`, `gwes.dll` and `ceshell.dll` come from a patched CE 6 build linked from Microsoft Platform Builder libraries. `serialusbfn.dll` is Microsoft's USB serial client from Sharp's SDU v5.2 updater image, and `rndisfn.dll` is Microsoft's RNDIS client from NXP's i.MX28 EVK CE 6.0 image.

| File | Bytes | SHA-256 | Use |
| --- | --- | --- | --- |
| `explorer.exe` | 288768 | `11bd4ac4259f8c7214ebebe2ad84ba61920a5454f979ad87ac2eaff758ae8f72` | The desktop on every model, from the card (`pwsh2-boot/PROFILE/explorer.exe`), with Restart in the Start menu and Brain in Start > Settings |
| `rndisfn.dll` | 27136 | `b0b5f7fdeaac85dca4e63f96b31dd92f642e4942084a6d7d72d4a103e6ab89a7` | USB networking (RNDIS), for Debug Agent and network apps, from the card (`pwsh2-boot/Windows/`) on every model |
| `serialusbfn.dll` | 35840 | `ab867ff19ce7cb2e3abff07390d71578c59c1c8456d6da0843bb6e8ee433145b` | Sharp's USB serial client (`04dd:9244`), for Debug Agent, from the card on every model |
| `pwa7300/gwes.dll` | 766464 | `292c5b647b79f6eba532f95ce6d9b01bfc0df05299801d886b594437228f1593` | Replaces the PW-A7300's `gwes.dll`; draws a mouse cursor |
| `pwa7300/ceshell.dll` | 292864 | `c81ecdadcfbf50763609a6ac99606a9996f98633a338fe73a297ac416c7db330` | Replaces the PW-A7300's `ceshell.dll`, for `explorer.exe` |
