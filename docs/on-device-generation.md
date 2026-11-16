# On-Device Generation

Design for making `boot.nkp` and its modules on the Brain at first boot, so a release card carries only our files and the profiles. Not implemented.

## Why

Today `boot.nkp` must be made from each user's own image, on the [patch maker](https://grimace-sdk.github.io/brainsurgeon/) page or with `tools/make-boot-nkp.mjs`. The output is derived from Sharp's image:

| Output | Sharp-derived content |
| --- | --- |
| `boot.nkp` | Every patch record carries the original bytes it replaces: ROM header, file table entries (copied from `EdDeskTop.bmp`'s entry), module names, the `Launch50` string, word patch and A7300 display originals |
| `modules/default.fdf` | A modified copy of Sharp's whole boot registry (about 170 KB) |
| `modules/keybd_EDNA2.dll` (A7300) | Sharp's keyboard driver, extracted and patched |
| `CPLBRAIN.CPL` (A7300) | Sharp's `cplmain.cpl`, extracted with its imports redirected |

If the Brain makes these itself, nothing derived from Sharp's image is ever distributed, and a release card works on every recognised Brain without a computer step. One card serves every model: it carries a U-Boot per model and one generator and inputs file for all of them (see [One Card, Several Models](#one-card-several-models)).

## Overview

A separate program, the generator (`nkgen.bin`), does the work. It is a pure RAM-to-RAM transform: Sharp's image and one inputs file go in, a list of output files comes out. It has no drivers, file system or U-Boot dependencies, so the same C sources build for the host and for the Brain.

U-Boot's part stays small:

1. Read Sharp's image (it already does) and load `nkgen.bin` and the inputs file (`nkgen.pak`) into RAM.
2. Call the generator with a parameter block.
3. Write the returned files to the card cache, then boot from them as it does today.

Later boots find the cache and never load the generator.

The JS generator stays the reference implementation. A host build of the C generator must give byte-identical files for every known image.

## Card Layout

| Path | Contents | Shipped or made |
| --- | --- | --- |
| `pwsh2-boot/nkgen.bin` | Generator, ARM binary with a header | Release |
| `pwsh2-boot/nkgen.pak` | Generator inputs for every profile: manifest, our modules, the A7300 `binaries/pwa7300` files, profile card files, registry additions, word patches, `bstart.exe`, skip list, A7300 display hook | Release |
| `pwsh2-boot/nk-CRC/boot.nkp` | Patch bundle. `CRC` is the image identity CRC, 8 lowercase hex digits | Made on the Brain |
| `pwsh2-boot/nk-CRC/modules/*` | Our drivers and the A7300 `gwes.dll` and `ceshell.dll` (copied out of the pak), the merged `default.fdf`, ROM-derived modules | Made on the Brain |
| `pwsh2-boot/nk-CRC/cache.txt` | Stamp: what the cache was made from, written last | Made on the Brain |
| `pwsh2-boot/nk-CRC/nkgen.log` | Generator log from the last generation or failure for this image | Made on the Brain |
| `pwsh2-boot/nk-CRC/boot-status.txt` | Patch status of the last boot of this image | Made on the Brain |
| `pwsh2-boot/PROFILE/` | Profile card files: for `pwa7300`, `explorer.exe`, and in `Windows/` `CPLBRAIN.CPL` (ROM-derived), `braincrypt.dll`, `brainras.dll`, `brainsip.dll` | Made on the Brain |
| `pwsh2-boot/boot-status-MODEL.txt` | Status of the last boot by this U-Boot build that found no cache folder | Made on the Brain |

All other release card files (the U-Boots, `EDSA2CFG.BIN`, `autorotate.exe`, `crashlog.exe`, `pwsh2-boot/Windows/brain-control.cpl`, the launch files) are ours, common to every model, and ship as now.

Profile card files move out of the shared `pwsh2-boot/Windows/` and `explorer/` into `pwsh2-boot/PROFILE/`, so one model's files never reach another. `profiles.json` changes to match (`pwsh2-boot/pwa7300/Windows/CPLBRAIN.CPL`, `pwsh2-boot/pwa7300/explorer.exe`), for the JS tools too, and bstart copies `pwsh2-boot/PROFILE/Windows/` into `\Windows` after `pwsh2-boot/Windows/`. The A7300 launch file names `\Storage Card\pwsh2-boot\pwa7300\explorer.exe`.

## Image Identity

The cache folder is named by a CRC-32 of the image's ROM header and its module and file tables. These hold every module's and file's name pointer, size, time and section pointers, so a different build gives a different CRC. They sit in one contiguous range: ROMHDR (84 bytes) at `pTOC`, then `nummods` 32-byte module entries, then `numfiles` 28-byte file entries.

U-Boot reads it before the skipping read, from the slot found in the trailer:

1. Read the slot's first sector. Check `ECEC` at `0x40`; `pTOC` is the word at `0x44`. The ROMHDR offset is `pTOC - 0x80200000`, which must lie inside the image.
2. Read 32 sectors (16 KB) from the sector holding ROMHDR. `nummods` is at ROMHDR `+16`, `numfiles` at `+48`.
3. The range is `84 + nummods * 32 + numfiles * 28` bytes. Refuse anything over 64 KB. If it runs past the 16 KB already read, read the rest.
4. CRC-32 the range (`u-boot/crc.h`).

Cost: two or three `blk_dread` calls totalling 16 to 64 KB, and a CRC over 10 to 20 KB, a few milliseconds in all; to be measured with the boot log timer. It needs about 30 lines in `sharpce.c` and a 64 KB buffer from `malloc`. The generator computes the same CRC from the image in RAM and returns it; U-Boot checks the two agree before writing the cache. `rom-image.mjs` gains `imageIdentity()` so the JS tools and tests can compute it too.

A full-image hash on later boots is not needed. Two different images with the same tables would have to come from the same Brain model, and moving a card to a different model also changes the first-stage loader EBOOT runs (`EDSA2EXE.BIN` for ED-SA2, `EDSA1EXE.BIN` for ED-SA1, `EDSH4EXE.BIN` for ED-SH4). A Sharp update on the same Brain changes the module tables, so it changes the CRC.

### Bundle Lookup

U-Boot looks for a bundle in this order:

1. `pwsh2-boot/nk-CRC/boot.nkp`, made on the Brain, by the page or CLI, or by hand.
2. If none, and `nkgen.bin` and `nkgen.pak` are present and valid, and `pwsh2-boot/no-nkgen` is absent: generate into `nk-CRC/`.
3. `pwsh2-boot/nk-LENGTH/boot.nkp`, as today: made by older versions of the patch maker page and CLI, or the pw-sh2 card build.
4. `pwsh2-boot/boot.nkp`.
5. None: unpatched CE.

Existing `nk-LENGTH` bundles keep working on cards with no generator, or with the `no-nkgen` marker, which also suits development with the CLI's extra options. On a release card with the generator, a generated `nk-CRC` bundle takes precedence over an older `nk-LENGTH` one; if generation fails, the `nk-LENGTH` bundle is still used. `bootsharpce info` prints the CRC. A `nk-CRC` bundle with no `cache.txt` (from the page, the CLI or by hand) is used as it stands and blocks generation for that image; delete it to go back to the generator.

### Page and CLI Transition

The patch maker page and `make-boot-nkp.mjs` move to `nk-CRC` in two releases, and U-Boot keeps reading `nk-LENGTH` indefinitely (one failed `fs_read`, a few milliseconds):

| Release | Page and CLI | U-Boot |
| --- | --- | --- |
| First with the generator | Write `nk-LENGTH` by default; print the CRC; `--folder crc` writes `nk-CRC` | Reads `nk-CRC`, then `nk-LENGTH` |
| Next | Write `nk-CRC` by default; `--folder length` (a page option, "for U-Boot before vX") for cards with an older U-Boot | Unchanged |

Bundles from the page and CLI also gain the `bootstat.txt` slot (see [Patch Status](#patch-status)), since they share the generator's `bstart` section. An older U-Boot applies them as before and leaves the slot empty.

## First Boot, Step by Step

U-Boot side (`bootsharpce`):

| Step | Action | Stage text | Progress |
| --- | --- | --- | --- |
| 1 | Find the slot and length from the trailer (unchanged) | | |
| 2 | Read the identity range and compute the CRC | | |
| 3 | `nk-CRC/cache.txt` valid: take the normal path (skipping read, apply, boot) | | |
| 4 | Load and check `nkgen.bin` and `nkgen.pak`. Missing or invalid: continue the [lookup](#bundle-lookup) | | |
| 5 | Read the whole image, no skips | `Reading Windows CE` | 0 to 35% |
| 6 | Call the generator | Generator's own | 35 to 75% |
| 7 | Write the outputs, then `cache.txt` | `Saving to card` | 75 to 90% |
| 8 | Zero the skip spans in RAM, apply `boot.nkp` from RAM, with `load` lines served from the output list | `Patching` | 90 to 100% |
| 9 | Write the status into the `bootstat.txt` slot in RAM and to the card if it changed, start CE | | |

Step 8 means a first boot never depends on the card write succeeding. The skip spans are zeroed after generation because the generator needs to see the whole image, and the `load` check requires a cleared target.

Generator side, mapped to the JS:

| Step | JS | What it does |
| --- | --- | --- |
| G1 | `rom-image.mjs` `isImage`, `openImage` | Check `ECEC`, parse ROMHDR, base `0x80200000` |
| G2 | `profiles.mjs` `findProfile`, `bytes.mjs` `sha256Hex` | Match the image by length, then SHA-256, against the manifest. No length match: no hash needed (every skip-list tested image has a profile) |
| G3 | `profiles.mjs` `deriveFromRom`, `rom-dll.mjs`, `pe-imports.mjs` | Make ROM-derived modules and card files: extract a relocatable DLL, redirect imports, check-and-patch bytes |
| G4 | `boot-nkp.mjs` `skipSpans` | Skip spans from the skip list, merged, aligned, checked against kept regions |
| G5 | `registry-edit.mjs`, `flat-registry.mjs` | Read `default.fdf`, set `init\Launch50`, apply profile registry and the `Software\Brain` model keys, write it back |
| G6 | `boot-nkp.mjs` `appendFileRecords`, `bstartPatch` | `bstart` section: append `bstart.exe` and the 4 KB `bootstat.txt` slot, reserve file entries, rewrite `Launch50` in place |
| G7 | `a7300-display.mjs`, `appendCodeRecords` | A7300 display hook section |
| G8 | `moduleReplacementPatch`, `fileRepointPatch` | One section per module: carve a span, add or repoint a file entry, rename Sharp's module, `load` line |
| G9 | `word-patch.mjs` | One section per word patch file |
| G10 | `nk-patch.mjs` `makePatchRecords`, `formatPatch` | Format records (64-byte runs, lowercase unpadded hex) and the bundle text |
| G11 | `profiles.mjs` `makeCard` | Output list: `boot.nkp`, `modules/*`, the profile's card files (from the pak or ROM-derived). Outputs copied from the pak point into it, so they take no arena space |

## C Port Plan

| JS module | Lines | C file | Est. C lines | Notes |
| --- | --- | --- | --- | --- |
| `bytes.mjs` | 71 | `util.c` | 200 | Byte-wise LE reads only (see [Pitfalls](#port-pitfalls)), hex, CRC-32, arena allocator, `mem*`, stable merge sort |
| WebCrypto SHA-256 | | `sha256.c` | 150 | Own implementation, so the host and ARM builds share it |
| `rom-image.mjs` | 252 | `romimage.c` | 350 | Header, module, file and copy tables, used regions. `readSlotImage` and dump handling are not needed |
| `lzx.mjs` | 287 | not ported | 0 | Only reached by `readFileContents` on a compressed file. `bstartPatch` already requires an uncompressed `default.fdf`, and `rom-dll` requires uncompressed sections, so no supported path decompresses |
| `flat-registry.mjs`, `registry-edit.mjs` | 220 | `registry.c` | 400 | Parse, set, pad to a word, write. Unchanged values keep their raw bytes, as in JS. `.reg` text is converted at release (see [Manifest](#release-manifest)), so no `.reg` parser |
| `nk-patch.mjs`, `word-patch.mjs` | 74 | `records.c` | 150 | Record splitting and text formatting |
| `boot-nkp.mjs` | 294 | `bootnkp.c` | 650 | Span carving, file appends, module sections |
| `a7300-display.mjs` | 99 | `a7300.c` | 180 | Table of words and trampolines |
| `debug-log.mjs` | 48 | not ported | 0 | CLI option only; stays in JS |
| `rom-dll.mjs` | 259 | `romdll.c` | 450 | Relocation recovery: the literal-target `Set` becomes a bitmap over the module's words; code ranges a sorted array |
| `pe-imports.mjs` | 148 | `peimports.c` | 280 | |
| `profiles.mjs` | 118 | `generate.c` | 250 | Orchestration and output list |
| | | `manifest.c` | 200 | Pak reader and checks |
| | | `entry.S`, `nkgen.ld` | 40 | Header, entry, BSS clear by U-Boot |
| | | `host.c` | 200 | Host driver: files in, card tree out |
| `nk-image-builder.mjs`, `pe-image.mjs`, `zip.mjs` | | not ported | 0 | Image building and the web page zip only |

About 3,500 C lines for about 1,600 JS lines, in about 14 files under `nkgen/`. Expected binary size 40 to 60 KB. Built with `arm-none-eabi-gcc -mcpu=arm926ej-s -marm -O2 -ffreestanding -nostdlib -mno-unaligned-access`, linking `libgcc` for division, in `build.sh` beside `ce/debug-log` and `ce/a7300-display`.

### What U-Boot Already Has

| Need | Status |
| --- | --- |
| `boot.nkp` applier | `nkpatch.c`. Needs two small changes: take the bundle from a RAM buffer, and resolve `load` paths from an in-RAM table before the card |
| FAT write, `mkdir`, `unlink` | Present (`FAT_WRITE` is selected by `SHARP_WINCE`). No `rename` |
| CRC-32 | `u-boot/crc.h`, used by `load` checks |
| SHA-256 | Not enabled. The generator does its own, so U-Boot needs none |
| Progress bar | `brain_menu_progress(done, total)`, being extended by another agent to show stage text |
| Console log to card | `savelog` and the boot log; generator log lines also go through it |

### Port Pitfalls

- ARM926 `LDR` from an unaligned address does not fault: it rotates the word. All multi-byte reads go through byte helpers, and `-mno-unaligned-access` plus `-Wcast-align=strict` keep the compiler honest. The host build cannot catch this, so the ARM build is also checked (milestone 5).
- JS numbers are doubles. Span and offset arithmetic that can go negative in JS (`span.end - length`) uses signed 64-bit in C so the same `check` fails.
- `char` is unsigned on ARM and signed on x86: use `uint8_t` throughout.
- Hex output must match `toString(16)`: lowercase, no padding, except where JS pads.
- JS `sort` is stable. Use a stable merge sort wherever the order of equal keys reaches the output.
- Name comparisons use `toLowerCase`. C folds ASCII only. Sharp's non-ASCII key names never equal our ASCII names in practice; the equivalence test covers the known images.
- Error messages keep the JS `check` wording, so a failure reads the same on the page and in `uboot.log`.

## Release Manifest

`nkgen.pak` is made at release by `tools/make-nkgen-pak.mjs` from `profiles/profiles.json`, `tools/skip-modules.json`, the fetched `inputs/` (checked against their pinned SHA-256) and the build output. Neither U-Boot nor the generator parses JSON or `.reg` text. `node tools/make-nkgen-pak.mjs --describe nkgen.pak` prints it as text for review.

Little-endian, all offsets from the start of the file, all records 4-byte aligned.

```
header (72 bytes)
  magic            "NKGENPAK"
  format           u32   1
  length           u32   whole file
  crc32            u32   of bytes 20 to length
  release          u32   string offset: brainsurgeon version
  strings          u32   offset, u32 length
  images           u32   offset, u32 count
  profiles         u32   offset, u32 count
  operations       u32   offset, u32 count
  blobs            u32   offset, u32 count
  bstart           u32   blob index
  skip_list        u32   blob index

image (48 bytes)
  sha256           32 bytes
  length           u32
  profile          u32   index
  device           u32   string offset
  description      u32   string offset

profile (16 bytes)
  id               u32   string offset
  first_operation  u32
  operation_count  u32
  flags            u32   0

operation (16 bytes)
  kind             u32   1 module_file, 2 module_rom, 3 registry, 4 word_patch, 5 a7300_display, 6 card_file, 7 card_file_rom
  name             u32   string offset: module name, card path, "registry" or "a7300-display"
  blob             u32   index
  argument         u32   0

blob (12 bytes)
  offset           u32
  length           u32
  crc32            u32
```

Strings are ASCII and zero-terminated. Equal blobs are stored once. Structured blobs are u32 words, with strings as pak string offsets:

| Kind | Blob |
| --- | --- |
| `module_file`, `card_file` | The file: from `inputs` (checked against its pinned SHA-256), `binaries/` (checked against the SHA-256 in its README) or the build |
| `module_rom`, `card_file_rom` | module, spare sections, redirect count, patch count; redirects (from, to, first import, import count); imports (ordinal or `ffffffff`, name or 0, new name); patches (offset, length, original and replacement as offsets in the blob, purpose); then the patch bytes |
| `registry` | count; entries (kind 0 key or 1 value, hive, key, name, type 1 or 4, value, source). `value` is the dword or a string. `source` 1 or 2 takes the string from the matched image's device or description, for the `Software\Brain` model keys |
| `word_patch` | module, count; words (address, original, replacement, purpose) |
| `a7300_display` | `a7300-display.bin` |
| skip list | minimum span, module count, file count, tested image count; module names, file names; tested SHA-256s, 32 bytes each. Every tested image must have a profile, so the generator hashes only images whose length a profile lists |

Each profile has one `registry` operation: its `.reg` files, parsed at release, then the model keys.

Operation order follows `profiles.json`, which fixes the order of sections and span carving and so the output bytes.

The pak is about 1.8 MB, most of it the A7300's `gwes.dll` (766 KB), `ceshell.dll` (293 KB) and `explorer.exe` (289 KB). With them in the pak the release card is turnkey: nothing has to be copied from the release's `binaries/` by hand.

Module DLLs live in the pak and are also written into `nk-CRC/modules/`, because `boot.nkp` `load` paths are relative to the bundle folder. That costs about 1.5 MB of card space on the A7300 and 0.1 MB on the PW-SH2, which is acceptable. A first A7300 boot writes about 1.8 MB to the card.

## Generator ABI

The user asked for either U-Boot's standalone application ABI (`go`, the exported jump table) or a bare-metal blob with a fixed entry and parameter block. This design uses the bare-metal blob:

- No dependency on U-Boot's `XF_VERSION`, `gd` register or export list, so `nkgen.bin` and `EDSA2EXE.BIN` version separately.
- The host build calls the same entry with the same block.
- The generator links nothing from U-Boot, so it stays MIT.
- It matches `ce/debug-log` and `ce/a7300-display`.

The generator needs no `malloc` (it gets an arena) and no `printf` (it gets a log callback with finished strings).

### Binary Header

`nkgen.bin` is linked at a fixed address and starts with:

```c
struct nkgen_header {
	char magic[8];
	uint32_t header_version;
	uint32_t abi_version;
	uint32_t link_address;
	uint32_t file_length;
	uint32_t bss_length;
	uint32_t entry_offset;
	uint32_t payload_crc32;
	uint32_t pak_format;
	char version[32];
};
```

`magic` is `"NKGENBIN"`. `payload_crc32` covers the bytes after the header.

### Call

```c
struct nkgen_output {
	const char *path;
	const uint8_t *data;
	uint32_t length;
	uint32_t crc32;
};

struct nkgen_call {
	uint32_t magic;
	uint32_t abi_version;
	uint32_t size;
	const uint8_t *image;
	uint32_t image_length;
	const uint8_t *pak;
	uint32_t pak_length;
	uint8_t *arena;
	uint32_t arena_length;
	uint32_t flags;
	void (*progress)(void *context, const char *stage, uint32_t done, uint32_t total);
	void (*log)(void *context, const char *line);
	void *context;
	int32_t result;
	uint32_t output_count;
	const struct nkgen_output *outputs;
	uint32_t step_count;
	const struct nkgen_step *steps;
	uint32_t known;
	const char *profile;
	uint32_t identity_crc32;
	uint8_t image_sha256[32];
	char message[160];
};

struct nkgen_step {
	const char *name;
	int32_t result;
	const char *detail;
};

int32_t nkgen_generate(struct nkgen_call *call);
```

- `image` is read-only to the generator. U-Boot patches it only after the call returns.
- Output paths are relative to the card root, with the folder already named `nk-CRC`. U-Boot accepts only paths under `pwsh2-boot/` with no `..`, so a bad pak cannot overwrite a U-Boot or another model's launch file.
- `result` is 0, or an error: bad call block, bad pak, check failed (with `message`), arena full. An unknown image is not an error: `known` is 0 and the bstart-only outputs are returned.
- `steps` lists each generator step (identify, ROM modules, registry, each section) with its result and a short detail, for the [patch status](#patch-status).
- `log` receives the JS `notes` and per-step summaries. U-Boot prints them to the console and keeps them for `nkgen.log`.
- Stack use is bounded and checked at build time with `-fstack-usage` (target under 8 KB); the generator runs on U-Boot's stack with interrupts as U-Boot left them (none in use).

### How U-Boot Finds and Validates It

1. `fs_size` and `fs_read` `pwsh2-boot/nkgen.bin` to `CONFIG_SHARP_WINCE_NKGEN_ADDRESS`.
2. Check `magic`, `header_version`, `abi_version` equal to U-Boot's, `link_address` equal to the load address, `file_length` equal to the file size, `file_length + bss_length` within the reserved region, and `payload_crc32`.
3. Read `nkgen.pak`; check its magic, `format` against the header's `pak_format`, length and CRC-32.
4. Zero the BSS, flush the D-cache over the code range and invalidate the I-cache, then call `link_address + entry_offset`.

There is no signature check. Anyone who can write `nkgen.bin` can also replace `EDSA2EXE.BIN`, so a pinned hash in U-Boot would only couple the two release cycles.

## Memory Placement

128 MB of RAM at `0x40000000` on every CE model (U-Boot's common code already uses `0x46000000`).

| Range | Use | Status |
| --- | --- | --- |
| `0x40000000`-`0x40100000` | Vectors, EBOOT scratch (`0x400C1000`-`0x400E0000`), BSP args | Existing |
| `0x40100000`-`0x401D1000` | EBOOT splash, menu background | Existing |
| `0x40180000` or `0x401E0000`, 64 KB | CE debug ring (A7300, PW-SH2) | Existing |
| `0x40200000`-`0x42000000` | Sharp's image, at most 30 MB less a sector, plus up to 64 KB extension | Existing |
| `0x42000000`-`0x42100000` | `nkgen.bin` code, data and BSS (1 MB) | New |
| `0x42100000`-`0x42800000` | `nkgen.pak` (7 MB; about 1.8 MB used) | New |
| `0x42800000`-`0x44000000` | Generator arena: work memory and outputs (24 MB) | New |
| `0x44000000`-`0x442BC000` | Display buffers and menu canvas | Existing |
| `0x45000000`-`0x45800000` | `boot.nkp` buffer | Existing |
| `0x45800000`-`0x45810000` | Debug log staging | Existing |
| malloc, 64 KB, freed after use | Identity range read | New |
| `0x46000000` | EBOOT register snapshot | Existing |
| About `0x47A00000` up | Relocated U-Boot, 4 MB malloc, stack | Existing |

The map is the same for every model's U-Boot build, so one `nkgen.bin` serves them all. `0x42000000` is also `CONFIG_LOADADDR` for the Linux path. The two never run in the same boot: generation happens only on the CE path, before the jump. All three new ranges are Kconfig values (`SHARP_WINCE_NKGEN_ADDRESS`, `_PAK_ADDRESS`, `_ARENA_ADDRESS`, `_ARENA_SIZE`). Peak arena use is expected under 4 MB (registry structures, a ROM DLL image, about 100 KB of bundle text), so the 24 MB is generous; the host build reports the high-water mark per image.

## Progress

The progress bar shows a short stage text and a number. Both U-Boot and the generator report through one callback shape:

```c
void progress(void *context, const char *stage, uint32_t done, uint32_t total);
```

`stage` is at most 24 characters and stays valid until the next call. `done` rises from 0 to `total` within a stage; a new `stage` starts again from 0. U-Boot maps the generator's calls into its 35 to 75% band and passes the stage text through, so the bar reads, for example, `Identifying image 40%`. The function U-Boot calls (`brain_menu_progress` with a stage argument, or a new `brain_menu_stage`) is for the progress work to settle; this design only needs it to take text plus done/total.

Generator stages and their share of its band:

| Stage text | Share |
| --- | --- |
| `Identifying image` (SHA-256, reported per MB) | 60% |
| `Extracting modules` (A7300 only) | 10% |
| `Merging registry` | 10% |
| `Building patches` | 20% |

On a 454 MHz ARM926, a software SHA-256 of 30 MB is estimated at 2 to 5 s; measure in milestone 5. Before the first generator call, the menu shows a line such as `Preparing Windows CE for this Brain (first start only)`, so the longer first boot is explained.

## Cache Invalidation

The folder name already pins the image (see [Image Identity](#image-identity)). `cache.txt`, written last, records the rest:

```
generator 0.4.0
pak 3f2a91c0
image-sha256 4bd8e2c4c519ddcf0cacb29c3ece428a5ea917e82e31441687a502097a9cfc68
image-length 1d4aebc
known yes
profile pwsh2
file pwsh2-boot/nk-8c1f22e0/boot.nkp e962 51a2c7f3
file pwsh2-boot/nk-8c1f22e0/modules/default.fdf 29ee9 0d4b11a2
```

- `generator` and `pak` are compared with the `nkgen.bin` header version and the pak CRC-32, read with two small reads. Any difference regenerates, so a new card release always rebuilds. If the generator is missing from the card, a cache is used as it stands.
- `image-sha256`, `known` and `profile` are kept for the [patch status](#patch-status), so later boots report them without hashing.
- `file` lines let U-Boot check `boot.nkp` after reading it. Modules are already checked by the `load` lines.
- A `boot.nkp` with no `cache.txt` was made by the page, the CLI, the pw-sh2 card build or by hand. U-Boot uses it as it stands and never overwrites it.
- Deleting `nk-CRC/cache.txt`, or `bootsharpce generate`, forces a rebuild of that image's cache only. There is no card-wide regenerate marker, since on a shared card the first model to boot would consume it.

## Patch Status

Each boot records how CE was patched, for Brain Control's About page and for bug reports.

### Status Text

U-Boot builds the status as `key=value` lines and puts it in three places:

- The `bootstat.txt` slot in RAM, for bstart. Every bundle's `bstart` section appends a 4 KB zero-filled ROM file, `bootstat.txt`, after `bstart.exe` (zeros over zeros, so it adds no records to `boot.nkp`, only a file entry and name). After applying the bundle, U-Boot finds `bootstat.txt` in the patched file table (about 20 lines: walk `numfiles` entries from the ROM header the identity read already located) and writes the status into it, truncated to 4 KB. bstart reads `\Windows\bootstat.txt`. This is per boot, needs no card write, and cannot be confused with another model's status on a shared card.
- `pwsh2-boot/nk-CRC/boot-status.txt`, the last boot of that image, for bug reports. U-Boot reads the old file (one small read) and writes it only when its contents change, so a normal cached boot costs no write.
- `pwsh2-boot/boot-status-MODEL.txt` instead, when there is no cache folder to write to (identity unreadable, or a `nk-LENGTH` or `boot.nkp` bundle). `MODEL` is the U-Boot build's `CONFIG_SHARP_WINCE_MODEL` (`pwsh2`, `pwa7300`, `pwsh4`); the PW-SH2 and PW-SB1 builds share `pwsh2`.

`pwsh2-boot/nk-CRC/nkgen.log` is written on each generation or generation failure for that image, replacing the previous one: the generator's log lines, step results and timings, and U-Boot's write results. Cached boots leave it alone. A failure with no CRC goes to `pwsh2-boot/nkgen-MODEL.log`.

The status text:

```
image=known
profile=pwsh2
device=PW-SH2
image-crc=8c1f22e0
image-length=1d4aebc
image-sha256=4bd8e2c4c519ddcf0cacb29c3ece428a5ea917e82e31441687a502097a9cfc68
bundle=cached
folder=pwsh2-boot/nk-8c1f22e0
generator=0.4.0
release=v0.5.0
error=
step identify=ok
step registry=ok
section bstart=applied
section module board.dll=applied
section words ymu818_wavdev.dll=skipped: expected bytes differ at line 3
```

| Key | Values |
| --- | --- |
| `image` | `known` (in the pak) or `unknown` |
| `profile`, `device` | From the pak; empty for an unknown image |
| `image-crc`, `image-length`, `image-sha256` | Identity CRC, length, SHA-256 (from `cache.txt` on cached boots; empty when no generator ran) |
| `bundle` | `generated` (made this boot), `cached` (`nk-CRC` with `cache.txt`), `existing` (any bundle with no `cache.txt`), `failed` (generation failed and nothing else was found), `none` (no bundle, unpatched CE) |
| `folder` | Bundle folder used |
| `generator`, `release` | `nkgen.bin` version and pak release; empty when not used |
| `error` | Generation or load error message, else empty |
| `step NAME` | Generator step result, on generated boots and copied from `nkgen.log`'s summary on cached ones |
| `section NAME` | `applied`, or `skipped:` and the applier's reason |

### Registry

bstart reads `\Windows\bootstat.txt` at startup, before it copies the card's `Windows` folders and starts the launch program, and writes it into the RAM registry under `HKLM\Software\Brain\Boot`. `profile` also tells bstart which `pwsh2-boot/PROFILE/Windows/` to copy, and is there for the per-model launch file choice:

| Value | Type | From |
| --- | --- | --- |
| `Image` | `REG_SZ` | `image` |
| `Profile` | `REG_SZ` | `profile` |
| `Device` | `REG_SZ` | `device` |
| `ImageCrc` | `REG_SZ` | `image-crc` |
| `ImageLength` | `REG_DWORD` | `image-length` |
| `ImageSha256` | `REG_SZ` | `image-sha256` |
| `Bundle` | `REG_SZ` | `bundle` |
| `Folder` | `REG_SZ` | `folder` |
| `Generator` | `REG_SZ` | `generator` |
| `Release` | `REG_SZ` | `release` |
| `Error` | `REG_SZ` | `error` |
| `SectionsApplied`, `SectionsSkipped` | `REG_DWORD` | Counts of `section` lines |

`HKLM\Software\Brain\Boot\Steps` and `HKLM\Software\Brain\Boot\Sections` hold one `REG_SZ` per `step` and `section` line, named by the step or section. bstart clears `Boot` first, so values never outlive the boot that set them. Going through bstart works for unknown images too, whose bstart-only bundle carries no registry copy. If the slot is missing or empty (an older bundle, or an older U-Boot), bstart writes `Bundle` = `unknown` only.

## One Card, Several Models

A release card boots the PW-SH2, PW-SB1, PW-SH4 and PW-A7300 (and PW-G5300). Each model's EBOOT runs its own U-Boot (`EDSA2EXE.BIN`, `EDSA1EXE.BIN`, `EDSH4EXE.BIN`, `EDNA3EXE.BIN`), built from one u-boot-brain commit. Everything else this design adds is shared or keyed so it cannot collide:

| Item | Shared or keyed | How |
| --- | --- | --- |
| `nkgen.bin` | Shared | Same CPU, same memory map and ABI in every U-Boot build. The release builds all U-Boots and the generator together, so their ABI versions match |
| `nkgen.pak` | Shared | Holds every profile; each image selects its own |
| Cache | Keyed by image | `nk-CRC/`, so each model's image has its own folder. Two Brains with the same image share one, which is correct |
| `nk-LENGTH` fallbacks | Keyed by image length | As today |
| `cache.txt`, `nkgen.log`, `boot-status.txt` | Keyed by image | Inside `nk-CRC/` |
| Boot status for bstart | Per boot, in RAM | `bootstat.txt` ROM slot, never on the card |
| Status with no cache folder | Keyed by U-Boot build | `boot-status-MODEL.txt`, `nkgen-MODEL.log` |
| Profile card files | Keyed by profile | `pwsh2-boot/PROFILE/`; bstart copies only its own profile's `Windows/` |
| Common card files | Shared | `pwsh2-boot/Windows/brain-control.cpl` and the CE programs, the same on every model |
| `no-nkgen` | Shared | Card-wide on purpose: a development setting |
| Launch files | Per model | Owned by the dual-model card work; `profile` in the status is available to choose them |
| `dump-nk` | Shared | Unchanged: the first model to boot consumes it |

Card space with an SH2 and an A7300 cache: about 1.8 MB for the pak, 0.3 MB for the SH2 cache, 1.8 MB for the A7300 cache and its card files.

Brain Control's About page shows `Image`, `Device`, `Bundle`, `Generator`, `Release`, the section counts and `Error`, with the steps and sections behind a details view.

## Failure Handling

| Case | Behaviour |
| --- | --- |
| No `nkgen.bin` or `nkgen.pak` | Continue the [lookup](#bundle-lookup): `nk-LENGTH/boot.nkp`, `pwsh2-boot/boot.nkp`, unpatched CE |
| Header, CRC or ABI check fails | Same, with the reason in the status and `nkgen.log` |
| Identity range unreadable or implausible (no `ECEC`, tables over 64 KB) | No generation; continue the lookup from `nk-LENGTH` |
| Unknown image | Best effort: the generator returns the bstart-only bundle the page makes for unknown images (no skips, no registry copy). U-Boot caches and boots it, with `image=unknown` in the status. `dump-nk` stays on the release card, so the image can be copied for the page or a bug report. A later release that knows the image has a new pak CRC, so it regenerates |
| A check fails (expected bytes differ, span too small) | Error with the JS message. No cache written; continue the lookup |
| Arena full | Error; fall back. The host build's high-water report should make this unreachable |
| Card full or write error | Files written so far are deleted, no `cache.txt`. This boot continues from the in-RAM outputs; the next boot tries again |
| Power loss or card pulled mid-write | No `cache.txt`, so the next boot regenerates and overwrites |
| Generator hangs or crashes | Not recoverable in U-Boot (no watchdog in use). Mitigated by host fuzzing with sanitizers and by the generator never writing outside its arena and output block |
| Cache valid but a section skips on apply | As today: that section is skipped and logged; the rest apply |

## Equivalence Testing

`tools/check-nkgen.sh` runs both generators on each image and compares:

1. `node tools/make-boot-nkp.mjs --image IMAGE --build BUILD --folder crc --card JS_CARD`
2. `nkgen-host --image IMAGE --pak nkgen.pak --card C_CARD`
3. `diff -r` of the two card trees, less the C side's `cache.txt`, `nkgen.log` and `boot-status.txt`: `pwsh2-boot/nk-CRC/` and `pwsh2-boot/PROFILE/`. Any difference fails.
4. Both folder names must equal `imageIdentity()` from `rom-image.mjs`.

It also runs each image with no profile (bstart-only) against `--no-profile`, and reports arena high-water and time per image.

Real-image equivalence runs locally only. Images come from a directory named by `NK_IMAGES`; the script refuses to run without it, and nothing in the repository names a local path. The known set is the five profile images (PW-SH2 V090 slot B, PW-SB1 slot B, PW-SH4 V100 slot B, PW-A7300, PW-G5300) plus the PW-SH2 V120 updater image, which has no profile and exercises the bstart-only path. The release checklist requires a passing local run.

Sharp's images never go into CI. CI gets:

- A synthetic fixture image, built by `tools/test/make-fixture-image.mjs` from our own DLLs: ROM header, module and file tables, an uncompressed `default.fdf`, modules with `.pdata` and imports, and a test profile in a test pak that exercises skips, module replacement and renaming, file repointing, word patches, ROM DLL extraction and import redirection. The A7300 display patch needs Sharp's driver at fixed addresses, so it is covered only by real images.
- Both generators on the fixture, compared as above.
- The host generator under ASan and UBSan on mutated fixtures (truncations, flipped table bytes): every case must return an error, never crash or read outside its inputs.
- The ARM build, checking the header and its stack-usage limit.

`uboot/tests/run.sh` gains the U-Boot side, in the existing stub harness: the identity range reader (against the fixture image, and truncated or corrupt first sectors), the `nkpatch.c` changes (bundle from RAM, `load` from the in-RAM table), the lookup order, the `cache.txt` parser, the `bootstat.txt` slot finder and the status writers. CI's `test` job runs the fixture check and `run.sh`.

## What Stays in JS

- `profiles/profiles.json` stays the source of truth. The pak is made from it at release.
- The patch maker page and `make-boot-nkp.mjs` stay as they are: the reference implementation, the tool for images with no profile, and the home of the development options (`--debug-log`, `--replace-module`, `--registry`, `--patch-words`, `--no-skip`, `--slot` on eMMC dumps).
- Their output keeps working on cards, in `nk-CRC` or `nk-LENGTH`, and is never overwritten ([transition](#page-and-cli-transition)).
- JS changes to stay the reference: `imageIdentity()`, `--folder`, the `bootstat.txt` slot in `boot-nkp.mjs` (about 25 lines), and the `pwsh2-boot/PROFILE/` card paths in `profiles.json`.
- `tools/nk/` and `build-nk-image.mjs` are unaffected.

## Size of the Change

| Part | Size |
| --- | --- |
| Generator (`nkgen/`) | About 3,500 C lines in 14 files, 40 to 60 KB binary |
| Release tool (`make-nkgen-pak.mjs`) and `--describe` | About 300 JS lines, 1 file |
| Fixture builder and check script | About 500 lines, 2 files |
| U-Boot (`u-boot-brain`) | About 490 lines, around 4 KB of binary. See below |
| JS reference changes | About 100 lines across `rom-image.mjs`, `boot-nkp.mjs`, `profiles.mjs`, `make-boot-nkp.mjs`, `web/app.mjs`, `profiles.json` |
| bstart and Brain Control | About 150 lines: `bootstat.txt` to the registry and the profile `Windows/` copy in `ce/bstart.c` (about 100), About page fields (about 50) |
| Build and packaging | `build.sh`, `package.sh`, CI workflow, `docs/reference.md`, `README.md` |

U-Boot, re-estimated:

| Part | File | Lines |
| --- | --- | --- |
| Identity range read and CRC | `sharpce.c` | 30 |
| First-boot flow: full read, call, in-RAM apply, stage text | `sharpce.c` | 50 |
| Load and validate `nkgen.bin` and `nkgen.pak`, cache flush, call | new `nkgen.c` | 110 |
| Write outputs and `cache.txt`, delete on failure | `nkgen.c` | 80 |
| Read and check `cache.txt` | `nkgen.c` | 40 |
| Status text, `nkgen.log` and `boot-status.txt` (write only on change), per-model fallback names | `nkgen.c` | 60 |
| Find the `bootstat.txt` slot in the patched file table and fill it | `nkgen.c` | 20 |
| Lookup order (`nk-CRC`, generate, `nk-LENGTH`, `boot.nkp`), `no-nkgen`, `bootsharpce generate` | `nkpatch.c`, `sharpce.c` | 30 |
| Bundle from RAM, `load` from the in-RAM table, section results for the status | `nkpatch.c` | 50 |
| Kconfig addresses, sizes and `SHARP_WINCE_MODEL` | `Kconfig`, defconfigs | 20 |

The rise from the first estimate of 330 lines is the identity read and the patch status.

## Milestones

| # | Milestone | Complexity |
| --- | --- | --- |
| 1 | JS reference changes: `imageIdentity()`, `--folder`, `bootstat.txt` slot, `pwsh2-boot/PROFILE/` paths. Pak format and `make-nkgen-pak.mjs`, including `binaries/pwa7300` and card files. Teach `makeCard` to run from a pak, and check JS from the pak equals JS from `profiles.json` on every image. Proves the pak is complete before any C | About 9 files, 550 lines |
| 2 | C core: util, SHA-256, ROM image and identity CRC, records, bstart section, host driver. Byte-identical bstart-only bundles on all six images | 7 files, about 1,300 lines |
| 3 | Registry, skip spans, module and file sections, word patches, step results. Byte-identical PW-SH2, PW-SB1 and PW-SH4 | 3 files, about 1,100 lines |
| 4 | ROM DLL extraction, import redirection, A7300 display. Byte-identical PW-A7300 and PW-G5300 | 3 files, about 900 lines |
| 5 | ARM build in `build.sh`, header, stack check, fixture image, sanitizer fuzzing, CI wiring. Measure SHA-256 and arena use | 6 files |
| 6 | U-Boot: identity read, lookup order, load and call, cache write and stamp, in-RAM apply, `no-nkgen`, `bootsharpce generate`, stage progress, status slot and files, `SHARP_WINCE_MODEL` in every CE defconfig. Host tests in `uboot/tests` | 5 U-Boot files, about 490 lines; 2 test files |
| 7 | bstart: `bootstat.txt` to `HKLM\Software\Brain\Boot`, profile `Windows/` copy. Brain Control About page | 3 files, about 150 lines |
| 8 | Release: `package.sh` ships `nkgen.bin` and `nkgen.pak` and drops `binaries/` from the card steps; the dual-model card carries every U-Boot and the A7300 launch file points at `pwsh2-boot/pwa7300/explorer.exe`; pw-sh2's card build stops shipping `nk-*` bundles; docs | About 10 files across the repositories, coordinated with the dual-model card work |
| 9 | Hardware: first and later boots on the PW-SH2, then the PW-SB1, PW-SH4 and PW-A7300; one card moved between a PW-SH2 and a PW-A7300; identity read timing; card-full and pulled-card cases; a local real-image equivalence run before release | Device time, with the user's go-ahead for each step |
| 10 | Next release: page and CLI write `nk-CRC` by default, `--folder length` kept | 3 files, small |

Milestones 2 to 4 are most of the work. Each ends in a passing equivalence run, so a half-done port is still checkable.

## Decisions

| Question | Decision |
| --- | --- |
| Cache folder name | `nk-CRC`, from the ROM header and tables read before the skipping read; `nk-LENGTH` still found after it |
| Real-image tests | Local only; CI uses the synthetic image |
| Unknown images | Best effort bstart-only bundle, recorded in the patch status |
| Duplicated modules | Accepted (about 1.5 MB on the A7300) |
| Later-boot check | Identity CRC only; a different model also has a different first-stage loader |
| Progress | Stage text plus done/total |
| A7300 `binaries/pwa7300` | In the pak, so the card is turnkey |
| Page and CLI folder | `nk-CRC`, after a one-release transition; `nk-LENGTH` stays readable |
| Dual-model card | One generator and pak; caches, logs and status keyed by image; profile card files in `pwsh2-boot/PROFILE/`; boot status to bstart through a RAM slot |

## Open Questions

1. Moving `explorer.exe` from `explorer/` to `pwsh2-boot/pwa7300/` and the A7300 card files out of `pwsh2-boot/Windows/` changes paths the current A7300 card and launch file use. Agree this with the dual-model card work before milestone 1.
