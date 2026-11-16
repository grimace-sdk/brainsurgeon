import { createHash } from 'node:crypto';
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { encodeLatin1, readU32, writeU32 } from '../lib/bytes.mjs';
import { redirectImports } from '../lib/pe-imports.mjs';
import { writeRegistry } from '../lib/registry-edit.mjs';
import { extractRomDll } from '../lib/rom-dll.mjs';
import { openImage } from '../lib/rom-image.mjs';

const usage = `Usage: node tools/test/make-fixture-image.mjs DIRECTORY

Makes a synthetic Windows CE image and a matching repository root for checking the generator without Sharp's images:
DIRECTORY/images (fixture.bin; unknown variants of the same length, of another length, and with no room for bstart below ulRAMStart), DIRECTORY/root
(profiles/, tools/skip-modules.json, inputs/, binaries/) and DIRECTORY/build (bstart.exe and the card files).`;

const output = process.argv[2];
if (!output || output === '--help') {
    console.log(usage);
    process.exit(output ? 0 : 1);
}

const imageBase = 0x80200000;
const workspaceLength = 0x200000;
const sourceBase = 0x41000000;
const movedDataAddress = 0x01f00000;
const patchedBase = 0x42000000;
const workspace = new Uint8Array(workspaceLength);
let cursor = 0x1000;

const align = (value, alignment) => Math.ceil(value / alignment) * alignment;
const place = (length, alignment = 4) => {
    cursor = align(cursor, alignment);
    const offset = cursor;
    cursor += length;
    return offset;
};
const va = (offset) => imageBase + offset;
const pattern = (length, seed) => Uint8Array.from({ length }, (_, index) => (index * 7 + seed * 13 + (index >> 8)) & 0xff);
const words = (values) => {
    const bytes = new Uint8Array(values.length * 4);
    values.forEach((value, index) => writeU32(bytes, index * 4, value >>> 0));
    return bytes;
};
const placeBytes = (bytes, alignment = 4) => {
    const offset = place(bytes.length, alignment);
    workspace.set(bytes, offset);
    return offset;
};
const placeString = (text) => placeBytes(encodeLatin1(`${text}\0`), 1);

const sourceText = new Uint8Array(0x400);
writeU32(sourceText, 0x00, 0xe59f0008);
writeU32(sourceText, 0x04, 0xe59f1008);
writeU32(sourceText, 0x08, 0xe12fff1e);
writeU32(sourceText, 0x10, sourceBase + 0x1100);
writeU32(sourceText, 0x14, movedDataAddress + 0x10);
writeU32(sourceText, 0x20, sourceBase + 0x1200);
for (let offset = 0x40; offset < 0x200; offset += 4) writeU32(sourceText, offset, 0xe1a00000 + offset);
writeU32(sourceText, 0x300, sourceBase + 0x2000);
writeU32(sourceText, 0x304, sourceBase);
writeU32(sourceText, 0x308, sourceBase + 0x3004);

const sourceRdata = new Uint8Array(0x100);
sourceRdata.set(words([0x2040, 0, 0, 0x2080, 0x2060]), 0);
sourceRdata.set(words([0x8000000a, 0x80000014, 0x20a0, 0x20b0, 0]), 0x40);
sourceRdata.set(encodeLatin1('COREDLL.dll\0'), 0x80);
sourceRdata.set(encodeLatin1('Named\0'), 0xa2);
sourceRdata.set(encodeLatin1('Other\0'), 0xb2);

const sourcePdata = words([sourceBase + 0x1000, 0x40 << 8 | 0x3, sourceBase + 0x1100, 0x40 << 8 | 0x3]);
const sourceData = words([sourceBase + 0x1000, movedDataAddress + 0x20, 0x12345678, 0, 0, 0, 0, 0, 0xcafe, 0, 0, 0, 0, 0, 0, 0]);

const patchedText = new Uint8Array(0x200);
for (let offset = 0; offset < patchedText.length; offset += 4) writeU32(patchedText, offset, 0xe3a00000 + offset);

const filler = (seed) => [{ rva: 0x1000, virtualSize: 0x12000, data: pattern(0x12000, seed), flags: 0x60000020, alignment: 0x1000 }];
const small = (seed, length = 0x800) => [{ rva: 0x1000, virtualSize: length, data: pattern(length, seed), flags: 0x40000040 }];

const modules = [
    { name: 'nk.exe', base: 0x80220000, sections: small(1) },
    { name: 'skip1.dll', base: 0x40100000, sections: filler(2) },
    { name: 'kept1.dll', base: 0x40200000, sections: small(3) },
    { name: 'skip2.dll', base: 0x40300000, sections: filler(4) },
    { name: 'replaced.dll', base: 0x40400000, sections: small(5) },
    { name: 'skip3.dll', base: 0x40500000, sections: filler(6) },
    {
        name: 'source.dll',
        base: sourceBase,
        virtualSize: 0x5000,
        imageFlags: 0x2102,
        entryRva: 0x1000,
        units: { 1: [0x2000, 40], 3: [0x3000, 0x10] },
        sections: [
            { rva: 0x1000, virtualSize: 0x400, data: sourceText, flags: 0x60000020 },
            { rva: 0x2000, virtualSize: 0x100, data: sourceRdata, flags: 0x40000040 },
            { rva: 0x3000, virtualSize: 0x10, data: sourcePdata, flags: 0x40000040 },
            { rva: 0x4000, virtualSize: 0x40, data: sourceData, flags: 0xc0000040, realAddress: movedDataAddress },
        ],
    },
    { name: 'skip4.dll', base: 0x40700000, sections: filler(7) },
    { name: 'patched.dll', base: patchedBase, sections: [{ rva: 0x1000, virtualSize: 0x200, data: patchedText, flags: 0x60000020 }] },
    { name: 'skip5.dll', base: 0x40900000, sections: filler(8) },
    { name: 'kept2.dll', base: 0x40a00000, sections: small(9) },
    { name: 'skip6.dll', base: 0x40b00000, sections: filler(10) },
    { name: 'kept3.dll', base: 0x40c00000, sections: small(11) },
];

for (const module of modules) {
    for (const section of module.sections) {
        section.dataOffset = placeBytes(section.data, section.alignment ?? 0x10);
        section.realAddress ??= module.base + section.rva;
    }
}

const registry = writeRegistry([
    { hive: 2, key: 'init', values: [
        { name: 'Launch50', type: 1, value: 'explorer.exe' },
        { name: 'Depend50', type: 3, value: '', raw: new Uint8Array([0x14, 0x00, 0x1e, 0x00]) },
    ] },
    { hive: 2, key: 'Drivers\\BuiltIn\\Test', values: [
        { name: 'Dll', type: 1, value: 'test.dll' },
        { name: 'Order', type: 4, value: 5 },
        { name: 'Paths', type: 7, value: ['first', 'second'] },
    ] },
    { hive: 1, key: 'ControlPanel', values: [{ name: '', type: 1, value: 'default' }] },
]);

const files = [
    { name: 'packed.bin', data: pattern(0x100, 12), realSize: 0x200, attributes: 0x801 },
    { name: 'EdDeskTop.bmp', data: pattern(0x400, 13), attributes: 0x1 },
    { name: 'default.fdf', data: registry, attributes: 0x1 },
    { name: 'data.bin', data: pattern(0x800, 14), attributes: 0x1 },
    { name: 'skipfile.bin', data: pattern(0x11000, 15), attributes: 0x1, alignment: 0x1000 },
];
for (const file of files) file.dataOffset = placeBytes(file.data, file.alignment ?? 4);

const copySource = modules[2].sections[0].dataOffset;
const copyEntry = placeBytes(words([va(copySource), 0x01f80000, 0x10, 0x20]));

for (const module of modules) {
    module.nameOffset = placeString(module.name);
    const e32 = new Uint8Array(112);
    e32[0] = module.sections.length;
    e32[2] = (module.imageFlags ?? 0x0102) & 0xff;
    e32[3] = (module.imageFlags ?? 0x0102) >> 8;
    writeU32(e32, 4, module.entryRva ?? 0x1000);
    writeU32(e32, 8, module.base);
    e32[12] = 6;
    writeU32(e32, 16, 0x10000);
    writeU32(e32, 20, module.virtualSize ?? align(0x1000 + module.sections.reduce((total, section) => total + section.virtualSize, 0), 0x1000));
    writeU32(e32, 32, 0x5f000000);
    for (const [unit, [rva, size]] of Object.entries(module.units ?? {})) {
        writeU32(e32, 36 + Number(unit) * 8, rva);
        writeU32(e32, 40 + Number(unit) * 8, size);
    }
    e32[108] = 9;
    module.e32Offset = placeBytes(e32);
    module.o32Offset = placeBytes(new Uint8Array(module.sections.length * 24));
    module.sections.forEach((section, index) => {
        workspace.set(words([section.virtualSize, section.rva, section.data.length, va(section.dataOffset), section.realAddress, section.flags]), module.o32Offset + index * 24);
    });
}
for (const file of files) file.nameOffset = placeString(file.name);

const romHeader = place(84 + modules.length * 32 + files.length * 28, 4);
const moduleTable = romHeader + 84;
const fileTable = moduleTable + modules.length * 32;
const physicalLast = cursor;
const ramStart = align(physicalLast + 0x8000, 0x1000);
workspace.set(words([0x80220000, 0x80240000, imageBase, va(physicalLast), modules.length, va(ramStart), va(ramStart) + 0x10000, va(ramStart) + 0x100000,
    1, va(copyEntry), 0, 0, files.length, 0, 0, 0, 0]), romHeader);
workspace[romHeader + 68] = 0xc2;
workspace[romHeader + 69] = 0x01;
modules.forEach((module, index) => {
    const size = module.sections.reduce((total, section) => total + section.data.length, 0);
    workspace.set(words([0, 0, 0, size, va(module.nameOffset), va(module.e32Offset), va(module.o32Offset), 0]), moduleTable + index * 32);
});
files.forEach((file, index) => {
    workspace.set(words([file.attributes, 0, 0, file.realSize ?? file.data.length, file.data.length, va(file.nameOffset), va(file.dataOffset)]), fileTable + index * 28);
});
workspace.set(encodeLatin1('ECEC'), 0x40);
writeU32(workspace, 0x44, va(romHeader));
const image = workspace.slice(0, physicalLast);

const sha256 = (bytes) => createHash('sha256').update(bytes).digest('hex');
const writeOutput = (path, contents) => {
    const location = join(resolve(output), path);
    mkdirSync(dirname(location), { recursive: true });
    writeFileSync(location, contents);
};

const derived = redirectImports(extractRomDll(openImage(image), 'source.dll', { spareSections: 2 }), 'COREDLL.dll', 'shim.dll', { 10: 'ShimTen', Named: 'ShimNamed' });
const textPointer = readU32(derived, 0x58 + 224 + 20);
const patchOffset = textPointer + 0x40;
const original = Buffer.from(derived.subarray(patchOffset, patchOffset + 4)).toString('hex');

const display = pattern(0x1800, 20);
const extra = pattern(0x1000, 21);
const unknown = image.slice();
unknown[modules[2].sections[0].dataOffset + 0x10] ^= 0xff;
const tight = image.slice();
writeU32(tight, romHeader + 20, va(align(physicalLast, 4) + 0x100));
const longer = new Uint8Array(image.length + 0x200);
longer.set(image);

const patchedWord = (offset) => readU32(patchedText, offset);
const definition = {
    inputs: [
        { url: 'https://github.com/grimace-sdk/fixture-display/releases/download/v1.0.0/fixture-display.dll', sha256: sha256(display), size: display.length, file: 'fixture-display.dll' },
    ],
    profiles: [
        {
            id: 'fixture',
            device: 'FIXTURE-1',
            images: [{ sha256: sha256(image), length: image.length, description: 'synthetic image' }],
            modules: [
                { name: 'replaced.dll', file: 'inputs/fixture-display.dll' },
                { name: 'added.dll', file: 'binaries/fixture/extra.dll' },
                { name: 'data.bin', file: 'build/data.bin' },
                {
                    name: 'source.dll',
                    rom: {
                        module: 'source.dll',
                        spareSections: 2,
                        redirectImports: [{ from: 'COREDLL.dll', to: 'shim.dll', imports: { 10: 'ShimTen', Named: 'ShimNamed' } }],
                        patchBytes: [{ offset: patchOffset.toString(16), original, replacement: 'e3a0002a', purpose: 'mov r0, #42' }],
                    },
                },
            ],
            registry: ['profiles/registry/fixture.reg'],
            wordPatches: ['profiles/patches/fixture.json'],
            cardFiles: [
                { path: 'pwsh2-boot/fixture/Windows/card.txt', file: 'build/card.txt' },
                { path: 'pwsh2-boot/fixture/Windows/derived.dll', rom: { module: 'source.dll', spareSections: 1, redirectImports: [{ from: 'COREDLL.dll', to: 'shim.dll', imports: { 20: 'ShimTwenty' } }] } },
            ],
        },
    ],
    sharedCardFiles: [{ path: 'pwsh2-boot/Windows/common.txt', file: 'build/card.txt' }],
};
const wordPatch = {
    module: 'patched.dll',
    words: [
        { address: (patchedBase + 0x1010).toString(16), original: patchedWord(0x10).toString(16), replacement: 'e3a00001', purpose: 'first word' },
        { address: (patchedBase + 0x1100).toString(16), original: patchedWord(0x100).toString(16), replacement: 'e12fff1e', purpose: 'second word' },
    ],
};
const skipList = {
    source: 'Synthetic fixture',
    tested_images: [sha256(image)],
    modules: ['skip1.dll', 'skip2.dll', 'skip3.dll', 'skip4.dll', 'skip5.dll', 'skip6.dll', 'missing.dll'],
    files: ['skipfile.bin'],
    minimum_span: 0x10000,
};
const registryText = `[HKEY_LOCAL_MACHINE\\Drivers\\BuiltIn\\Test]
"Dll"="replaced.dll"
"Order"=dword:7
[HKEY_LOCAL_MACHINE\\Fixture\\New]
"Text"="hello"
@="default value"
[HKEY_CURRENT_USER\\ControlPanel]
`;

writeOutput('images/fixture.bin', image);
writeOutput('images/fixture-unknown.bin', unknown);
writeOutput('images/fixture-length.bin', longer);
writeOutput('images/fixture-tight.bin', tight);
writeOutput('root/profiles/profiles.json', `${JSON.stringify(definition, null, 2)}\n`);
writeOutput('root/profiles/registry/fixture.reg', registryText);
writeOutput('root/profiles/patches/fixture.json', `${JSON.stringify(wordPatch, null, 2)}\n`);
writeOutput('root/tools/skip-modules.json', `${JSON.stringify(skipList, null, 2)}\n`);
writeOutput('root/inputs/fixture-display.dll', display);
writeOutput('root/binaries/fixture/extra.dll', extra);
writeOutput('root/binaries/fixture/README.md', `# Fixture Binaries\n\n| File | Bytes | SHA-256 | Use |\n| --- | --- | --- | --- |\n| \`extra.dll\` | ${extra.length} | \`${sha256(extra)}\` | Added module |\n`);
writeOutput('build/bstart.exe', pattern(0x2400, 22));
writeOutput('build/data.bin', pattern(0xc00, 23));
writeOutput('build/card.txt', 'fixture card file\n');
console.log(`${resolve(output)}: fixture image ${image.length} bytes, sha256 ${sha256(image)}`);
