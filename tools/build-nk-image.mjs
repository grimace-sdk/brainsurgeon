import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { parseArgs } from 'node:util';
import { sha256Hex } from './lib/bytes.mjs';
import { buildNkImage, describeBuild, makeImageBundle } from './lib/nk-image-builder.mjs';
import { imageFromDumpOrSlot } from './lib/rom-image.mjs';
import { parseWordPatch } from './lib/word-patch.mjs';

const usage = `Usage: node tools/build-nk-image.mjs --image NK_IMAGE [--slot A|B] --output DIRECTORY [--remove-sharp-only] [--remove NAME]... [--rename OLD=NEW]... [--module NAME=PE]... [--kernel-module NAME=PE]... [--file NAME=PATH]... [--registry ADDITIONS.reg]... [--patch-words PATCH.json]...

Builds a new Windows CE image from Sharp's: Sharp's modules and files are laid out again, without the ones removed, with ours added as execute-in-place modules, and with a new ROM header and tables. nk.exe and kernel.dll stay where they are.

NK_IMAGE is pwsh2-boot/nk-slot-<slot>.bin written by U-Boot, or a full eMMC dump (then --slot picks the slot).
--remove-sharp-only removes the modules and files in tools/skip-modules.json, which only Sharp's dictionary uses.
--remove drops a module or file. --rename renames a module, for example to keep Sharp's touchraw.dll as touchra_.dll behind a shim.
--module adds an EXE or a user-mode DLL; a NAME already in the image replaces it, in the same mode as the original. --kernel-module adds a kernel-mode DLL.
--file adds or replaces a ROM file.
--registry merges .reg files (strings and dwords) into the image's default.fdf.
--patch-words replaces checked 32-bit words in a module, by virtual address (see make-boot-nkp).

Writes DIRECTORY/nk.nb0 (the image), DIRECTORY/nk-body.bin (the image after its first page) and DIRECTORY/boot.nkp, which makes U-Boot load the body in place of Sharp's image and patch the first page.`;

const { values: options } = parseArgs({
    options: {
        image: { type: 'string' },
        slot: { type: 'string' },
        output: { type: 'string' },
        'remove-sharp-only': { type: 'boolean', default: false },
        remove: { type: 'string', multiple: true, default: [] },
        rename: { type: 'string', multiple: true, default: [] },
        module: { type: 'string', multiple: true, default: [] },
        'kernel-module': { type: 'string', multiple: true, default: [] },
        file: { type: 'string', multiple: true, default: [] },
        registry: { type: 'string', multiple: true, default: [] },
        'patch-words': { type: 'string', multiple: true, default: [] },
        help: { type: 'boolean', default: false },
    },
});
if (options.help || !options.image || !options.output) {
    console.log(usage);
    process.exit(options.help ? 0 : 1);
}

const pair = (argument) => {
    const separator = argument.indexOf('=');
    if (separator <= 0) throw new Error(`Expected NAME=VALUE, got ${argument}`);
    return [argument.slice(0, separator), argument.slice(separator + 1)];
};
const readModule = (kernel) => (argument) => {
    const [name, path] = pair(argument);
    return { name, contents: new Uint8Array(readFileSync(path)), ...(kernel ? { kernel: true } : {}) };
};

const source = imageFromDumpOrSlot(new Uint8Array(readFileSync(options.image)), options.slot);
const sharpOnly = JSON.parse(readFileSync(new URL('./skip-modules.json', import.meta.url), 'utf8'));
const result = buildNkImage({
    bytes: source.bytes,
    remove: [...(options['remove-sharp-only'] ? [...sharpOnly.modules, ...sharpOnly.files] : []), ...options.remove],
    rename: options.rename.map((argument) => {
        const [from, to] = pair(argument);
        return { from, to };
    }),
    modules: [...options.module.map(readModule(false)), ...options['kernel-module'].map(readModule(true))],
    files: options.file.map((argument) => {
        const [name, path] = pair(argument);
        return { name, contents: new Uint8Array(readFileSync(path)) };
    }),
    registryAdditions: options.registry.map((path) => readFileSync(path, 'utf8')),
    wordPatches: options['patch-words'].map((path) => parseWordPatch(readFileSync(path, 'utf8'))),
});
const bundle = makeImageBundle({ sourceBytes: source.bytes, built: result.bytes, path: 'nk-body.bin' });

mkdirSync(options.output, { recursive: true });
writeFileSync(join(options.output, 'nk.nb0'), result.bytes);
writeFileSync(join(options.output, 'nk-body.bin'), bundle.body);
writeFileSync(join(options.output, 'boot.nkp'), bundle.text);

console.log(`source image ${source.bytes.length} bytes, sha256 ${await sha256Hex(source.bytes)}`);
for (const line of describeBuild(result)) console.log(line);
console.log(`built image sha256 ${await sha256Hex(result.bytes)}`);
for (const note of result.notes) console.log(`note: ${note}`);
