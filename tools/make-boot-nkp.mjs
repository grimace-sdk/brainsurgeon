import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';
import { makeCard, makeCardFromPak } from './lib/profiles.mjs';
import { parseWordPatch } from './lib/word-patch.mjs';

const usage = `Usage: node tools/make-boot-nkp.mjs --image NK_IMAGE [--card CARD_DIRECTORY | --output boot.nkp] [--folder length|crc] [--build DIRECTORY | --pak nkgen.pak] [--root DIRECTORY] [--bstart BSTART_EXE] [--slot A|B] [--skip | --no-skip] [--no-profile] [--a7300-display HOOK_BIN] [--debug-log HOOK_BIN [--debug-log-address PHYSICAL]] [--replace-module NAME=DLL]... [--registry ADDITIONS.reg]... [--patch-words PATCH.json]...

NK_IMAGE is pwsh2-boot/nk-slot-<slot>.bin written by U-Boot, or a full eMMC dump (then --slot picks the slot).
An image listed in profiles/profiles.json gets that device's drivers and registry additions (PW-SH2, PW-SB1, PW-SH4, PW-A7300, PW-G5300). --no-profile adds only bstart and the options given.
--card writes boot.nkp and everything it loads into CARD_DIRECTORY, the card's root: pwsh2-boot/nk-LENGTH/ and the profile's card files. --output (default boot.nkp) writes boot.nkp and its modules/ folder only.
--folder crc names the bundle folder by the image identity CRC (nk-CRC), for U-Boot with the on-device generator; the default is the image length (nk-LENGTH).
--build is the folder with bstart.exe, a7300-display.bin and the CE programs (default build/ beside tools/; out/ after sh build.sh out). --bstart overrides bstart.exe.
--pak takes the profiles, inputs, skip list and bstart.exe from a generator pak (tools/make-nkgen-pak.mjs) in place of the repository and --build.
--root is the folder holding profiles/, tools/skip-modules.json, inputs/ and binaries/ (default: this repository).
Sharp-only modules are skipped by default for images the skip list was checked against, and whenever a profile applies, since its drivers load into the skipped space. --no-skip also turns the profile off.
--a7300-display adds the PW-A7300 portrait and tearing-effect display patch (A7300 main image only).
--debug-log adds the kernel debug log hook (ce/debug-log): debug strings are kept in a 64 KB RAM ring for U-Boot to save, at --debug-log-address (default 0x40180000; the PW-SH2 uses 0x401e0000, past EBOOT's splash buffers (0x40100000-0x401d1000)). It must match U-Boot's CONFIG_SHARP_WINCE_DEBUG_LOG_ADDRESS. The A7300 display patch already includes it.
--replace-module adds DLL as a ROM file named NAME, loaded by U-Boot from modules/NAME next to the bundle into a skipped span, and renames ROM module NAME (last letter of the name becomes _), so the loader picks DLL. Needs skipping on. A NAME that is not in the ROM is added as a new file. It takes the place of a profile module with the same NAME.
--registry merges .reg files (strings and dwords) into a copy of the ROM's default.fdf, loaded the same way as modules/default.fdf. It may be given more than once.
--patch-words replaces 32-bit words in a ROM module's uncompressed sections. PATCH.json is {"module": NAME, "words": [{"address", "original", "replacement", "purpose"}]} with hex strings; each original word is checked. U-Boot applies each patch file as one section, all or nothing.`;

const { values: options } = parseArgs({
    options: {
        image: { type: 'string' },
        card: { type: 'string' },
        output: { type: 'string' },
        folder: { type: 'string', default: 'length' },
        build: { type: 'string' },
        pak: { type: 'string' },
        root: { type: 'string' },
        bstart: { type: 'string' },
        slot: { type: 'string' },
        skip: { type: 'boolean', default: false },
        'no-skip': { type: 'boolean', default: false },
        'no-profile': { type: 'boolean', default: false },
        'a7300-display': { type: 'string' },
        'debug-log': { type: 'string' },
        'debug-log-address': { type: 'string' },
        'replace-module': { type: 'string', multiple: true, default: [] },
        registry: { type: 'string', multiple: true, default: [] },
        'patch-words': { type: 'string', multiple: true, default: [] },
        help: { type: 'boolean', default: false },
    },
});
if (options.help || !options.image || (options.card && options.output) || (options.skip && options['no-skip']) || (options.pak && (options.build || options.root))) {
    console.log(usage);
    process.exit(options.help ? 0 : 1);
}

options.output ??= options.card ? undefined : 'boot.nkp';
const root = resolve(options.root ?? join(dirname(fileURLToPath(import.meta.url)), '..'));
const build = resolve(options.build ?? join(root, 'build'));
const locate = (path) => (path.startsWith('build/') ? join(build, path.slice('build/'.length)) : join(root, path));
const hint = (path) => (path.startsWith('inputs/') ? '; run node tools/fetch-inputs.mjs' : path.startsWith('build/') ? '; pass --build' : '');
const readFile = async (path) => {
    const location = locate(path);
    if (!existsSync(location)) throw new Error(`${location} is missing${hint(path)}`);
    return new Uint8Array(readFileSync(location));
};
const extra = {
    a7300Display: options['a7300-display'] ? new Uint8Array(readFileSync(options['a7300-display'])) : undefined,
    debugLog: options['debug-log'] ? new Uint8Array(readFileSync(options['debug-log'])) : null,
    debugLogAddress: options['debug-log-address'] ? Number.parseInt(options['debug-log-address'], 16) : undefined,
    moduleReplacements: options['replace-module'].map((argument) => {
        const [name, path] = argument.split('=');
        return { name, contents: new Uint8Array(readFileSync(path)) };
    }),
    registryAdditions: options.registry.map((path) => readFileSync(path, 'utf8')),
    wordPatches: options['patch-words'].map((path) => parseWordPatch(readFileSync(path, 'utf8'))),
};
const skip = options.skip ? true : options['no-skip'] ? false : 'auto';
const contents = new Uint8Array(readFileSync(options.image));
const bstartPath = options.bstart ?? join(build, 'bstart.exe');
if (!options.pak && !existsSync(bstartPath)) {
    console.error(`${bstartPath} is missing; pass --build or --bstart`);
    process.exit(1);
}

const made = options.pak
    ? await makeCardFromPak({ contents, slot: options.slot, pak: new Uint8Array(readFileSync(options.pak)), skip, useProfile: !options['no-profile'], extra, folder: options.folder })
    : await makeCard({
        contents,
        slot: options.slot,
        bstart: new Uint8Array(readFileSync(bstartPath)),
        skipList: JSON.parse(readFileSync(join(root, 'tools/skip-modules.json'), 'utf8')),
        skip,
        definition: JSON.parse(readFileSync(join(root, 'profiles/profiles.json'), 'utf8')),
        readFile,
        useProfile: !options['no-profile'],
        extra,
        folder: options.folder,
    });

const inBundle = (file) => file.path.startsWith(`${made.folder}/`);
const destination = (file) => {
    if (options.card) return join(options.card, file.path);
    const relative = file.path.slice(made.folder.length + 1);
    return relative === 'boot.nkp' ? options.output : join(dirname(options.output), relative);
};
const written = made.files.filter((file) => options.card || inBundle(file));
for (const file of written) {
    mkdirSync(dirname(destination(file)), { recursive: true });
    writeFileSync(destination(file), file.contents);
}

console.log(`image ${made.imageLength} bytes, sha256 ${made.imageSha256}${made.identity ? `, identity ${made.identity}` : ''}`);
if (made.device) console.log(`${made.device} ${made.imageDescription}${made.profile ? `, profile ${made.profile}` : ''}`);
console.log(`${destination(made.files[0])}: ${made.patches.join(', ')}; ${made.spans.length} skip spans, ${made.skippedBytes} bytes (${Math.round(100 * made.skippedBytes / made.imageLength)}% of the image)`);
for (const module of made.replacedModules) {
    console.log(`${module.name}: ${module.length} bytes at ${module.address.toString(16)}, ${module.repointed ? "Sharp's ROM file entry repointed" : module.renamed ? `ROM entry renamed ${module.renamed}` : 'new ROM file'}`);
}
for (const file of written.slice(1)) console.log(`${destination(file)}: ${file.contents.length} bytes`);
const skipped = made.files.filter((file) => !written.includes(file));
if (skipped.length) console.log(`note: use --card to also write ${skipped.map((file) => file.path).join(', ')}`);
for (const note of made.notes) console.log(`note: ${note}`);
