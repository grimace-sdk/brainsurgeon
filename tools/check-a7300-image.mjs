import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { parseArgs } from 'node:util';
import { a7300DisplayImageSha256s } from './lib/a7300-display.mjs';
import { makeBootNkp } from './lib/boot-nkp.mjs';
import { crc32, readU32, sha256Hex } from './lib/bytes.mjs';
import { parseFlatRegistry } from './lib/flat-registry.mjs';
import { parseRegFile } from './lib/registry-edit.mjs';
import { fileTableVa, findFile, imageBase, listFiles, listModules, openImage, readFileContents } from './lib/rom-image.mjs';

const { values: options } = parseArgs({ options: { image: { type: 'string' }, bstart: { type: 'string' }, 'a7300-display': { type: 'string' },
    'replace-module': { type: 'string', multiple: true, default: [] }, registry: { type: 'string' } } });
assert.ok(options.image && options.bstart, 'Usage: node tools/check-a7300-image.mjs --image ROM --bstart BSTART [--a7300-display HOOK_BIN] [--replace-module NAME=DLL]... [--registry ADDITIONS.reg]');
const bytes = new Uint8Array(readFileSync(options.image));
assert.ok(a7300DisplayImageSha256s.includes(await sha256Hex(bytes)), 'This check is pinned to the acquired PW-A7300 and PW-G5300 main ROMs');
const bstart = new Uint8Array(readFileSync(options.bstart));
const display = options['a7300-display'] ? new Uint8Array(readFileSync(options['a7300-display'])) : null;
const moduleReplacements = options['replace-module'].map((argument) => {
    const [name, path] = argument.split('=');
    return { name, contents: new Uint8Array(readFileSync(path)) };
});
const registryAdditions = options.registry ? readFileSync(options.registry, 'utf8') : null;
const skipping = moduleReplacements.length > 0 || registryAdditions !== null;
const result = await makeBootNkp({ contents: bytes, bstart, a7300Display: display, moduleReplacements, registryAdditions,
    skipList: JSON.parse(readFileSync(new URL('./skip-modules.json', import.meta.url), 'utf8')), skip: skipping });
assert.equal(result.spans.length > 0, skipping);
const bundleSkips = result.bundle.split('\n').filter((line) => line.startsWith('skip '));
assert.equal(bundleSkips.length, result.spans.length);

let patched = Uint8Array.from(bytes);
for (const span of result.spans) patched.fill(0, span.start, span.end);
let records = 0;
const sections = result.bundle.trim().split(/^patch /m).slice(1);
assert.deepEqual(sections.map(section => section.split('\n')[0]),
    [...(display ? ['bstart', 'a7300-display'] : ['bstart']), ...result.replacedModules.map(replacement => `module ${replacement.name}`)]);
for (const section of sections) {
    const lines = section.trim().split('\n').slice(1);
    const header = lines.shift().match(/^image ([0-9a-f]+) ([0-9a-f]+)$/);
    assert.ok(header);
    assert.equal(Number.parseInt(header[1], 16), bytes.length);
    const extended = new Uint8Array(Math.max(patched.length, Number.parseInt(header[2], 16)));
    extended.set(patched);
    patched = extended;
    for (const line of lines) {
        const load = line.match(/^load ([0-9a-f]+) ([0-9a-f]+) ([0-9a-f]+) modules\/(\S+)$/);
        if (load) {
            const [offset, length, checksum] = load.slice(1, 4).map(value => Number.parseInt(value, 16));
            const replacement = result.replacedModules.find(candidate => candidate.name.toLowerCase() === load[4].toLowerCase());
            assert.ok(replacement, `No DLL given for ${load[4]}`);
            assert.equal(replacement.contents.length, length);
            assert.equal(crc32(replacement.contents), checksum);
            assert.ok(patched.subarray(offset, offset + length).every(byte => byte === 0), `load target ${offset.toString(16)} is not cleared`);
            patched.set(replacement.contents, offset);
            records += 1;
            continue;
        }
        const match = line.match(/^([0-9a-f]+) ([0-9a-f]+) ([0-9a-f]+)$/);
        assert.ok(match, `Unexpected patch record: ${line}`);
        const offset = Number.parseInt(match[1], 16);
        const expected = Buffer.from(match[2], 'hex');
        const replacement = Buffer.from(match[3], 'hex');
        assert.equal(expected.length, replacement.length);
        assert.ok(offset + expected.length <= patched.length);
        assert.deepEqual(Buffer.from(patched.subarray(offset, offset + expected.length)), expected);
        patched.set(replacement, offset);
        records += 1;
    }
}

const image = openImage(patched);
assert.deepEqual(Buffer.from(readFileContents(image, findFile(image, 'bstart.exe'))), Buffer.from(bstart));
const registry = parseFlatRegistry(readFileContents(image, findFile(image, 'default.fdf')));
const init = registry.keys.find(key => key.hive === 2 && key.key.toLowerCase() === 'init');
assert.ok(init);
assert.equal(init.values.find(value => value.name === 'Launch50').value, 'bstart.exe');
assert.equal(image.header.nummods, 158);
const addedFiles = result.replacedModules.filter(replacement => !replacement.repointed);
assert.equal(image.header.numfiles, 53 + addedFiles.length);
const files = listFiles(image);
assert.equal(files[0].entryVa, fileTableVa(image));
assert.equal(files[52].name, 'bstart.exe');
for (const replacement of result.replacedModules.filter(item => item.repointed)) {
    const file = findFile(image, replacement.name);
    assert.ok(files.indexOf(file) < 52, `${replacement.name} is not Sharp's entry`);
    assert.deepEqual(Buffer.from(readFileContents(image, file)), Buffer.from(replacement.contents));
    assert.ok(result.spans.some(span => file.loadOffset >= span.start && file.loadOffset + file.compressedSize <= span.end));
    console.log(`${replacement.name}: Sharp's ROM file entry now points at ${file.loadVa.toString(16)} (${file.realSize} bytes) inside a skipped span`);
}
for (const [index, replacement] of addedFiles.entries()) {
    const file = files[53 + index];
    assert.equal(file.name.toLowerCase(), replacement.name.toLowerCase());
    assert.equal(file.entryVa, files[52].entryVa + 28 * (1 + index));
    assert.deepEqual(Buffer.from(readFileContents(image, file)), Buffer.from(replacement.contents));
    const renamed = replacement.renamed;
    const modules = listModules(image);
    assert.ok(!modules.some(module => module.name.toLowerCase() === replacement.name.toLowerCase()), `${replacement.name} is still a ROM module`);
    assert.equal(files.filter(other => other.name.toLowerCase() === replacement.name.toLowerCase()).length, 1, `${replacement.name} is in the ROM more than once`);
    if (renamed) assert.ok(modules.some(module => module.name === renamed) || files.some(other => other.name === renamed));
    assert.ok(result.spans.some(span => file.loadOffset >= span.start && file.loadOffset + file.compressedSize <= span.end));
    console.log(`${replacement.name}: ROM file at ${file.loadVa.toString(16)} (${file.realSize} bytes) inside a skipped span; ${renamed ? `original renamed ${renamed}` : 'new file'}`);
}
if (registryAdditions) {
    for (const entry of parseRegFile(registryAdditions).filter(item => item.name !== null)) {
        const key = registry.keys.find(candidate => candidate.hive === entry.hive && candidate.key.toLowerCase() === entry.keyPath.toLowerCase());
        assert.ok(key, `${entry.keyPath} missing from default.fdf`);
        assert.equal(key.values.find(value => value.name.toLowerCase() === entry.name.toLowerCase())?.value, entry.value, `${entry.keyPath}\\${entry.name}`);
    }
    console.log(`Registry additions verified in default.fdf (${registry.keys.length} keys)`);
}

if (display) {
    const driverReplacement = result.replacedModules.findIndex(replacement => replacement.name === 'ddraw_mx28.dll');
    const driverName = driverReplacement >= 0 ? result.replacedModules[driverReplacement].renamed : 'ddraw_mx28.dll';
    const driver = listModules(image).find(module => module.name === driverName);
    const text = driver.sections[0];
    const word = address => readU32(patched, text.dataOffset + address - text.realAddress);
    const hookAddress = word(0xc06f1dd0);
    const hookOffset = hookAddress - imageBase;
    assert.equal(hookAddress % 32, 0);
    assert.deepEqual(Buffer.from(patched.subarray(hookOffset, hookOffset + display.length)), Buffer.from(display));
    const hookAtEnd = hookOffset >= bytes.length && image.header.physlast >= hookAddress + display.length;
    const hookInSpan = result.spans.some(span => hookOffset >= span.start && hookOffset + display.length <= span.end);
    assert.ok((hookAtEnd || hookInSpan) && image.header.physlast <= image.header.ulRAMStart);
    const target = (address) => (address + 8 + (((word(address) & 0x00ffffff) << 8) >> 6)) >>> 0;
    assert.equal(word(0xc06f6328), 0xe3a01008);
    assert.equal(word(0xc06f6390), 0xe3a01035);
    assert.equal(word(0xc06f49c0), 0xea00000b);
    assert.equal(target(0xc06f49c0), 0xc06f49f4);
    assert.equal(word(0xc06f63a0) >>> 24, 0xea);
    assert.equal(target(0xc06f63a0), 0xc06f1dc0);
    assert.equal(word(0xc06f655c) >>> 24, 0xeb);
    assert.equal(target(0xc06f655c), 0xc06f1dcc);
    assert.equal(word(0xc06f1dc0), 0xe3a00001);
    assert.equal(word(0xc06f1dc4), 0xe3a01000);
    assert.equal(word(0xc06f1dc8) >>> 24, 0xea);
    assert.equal(target(0xc06f1dc8), 0xc06f72e4);
    assert.equal(word(0xc06f1dcc), 0xe51ff004);
    assert.equal(readU32(display, 0) >>> 24, 0xea);
    assert.equal(readU32(display, 4) >>> 24, 0xea);
    assert.equal(word(0xc06fa770), 0xe51ff004);
    assert.equal(word(0xc06fa774), hookAddress + 4);
    assert.equal(readU32(display, 8) >>> 24, 0xea);
    assert.equal(word(0xc06f1944), hookAddress + 8);
    assert.equal(readU32(display, 12) >>> 24, 0xea);
    assert.equal(readU32(display, 16) >>> 24, 0xea);
    assert.equal(word(0xc06f82b0), 0xe1a00006);
    assert.equal(word(0xc06f82b4) >>> 24, 0xeb);
    assert.equal(target(0xc06f82b4), 0xc06f1dd4);
    for (const address of [0xc06f82b8, 0xc06f82bc, 0xc06f82c0, 0xc06f82c4]) assert.equal(word(address), 0xe1a00000);
    assert.equal(word(0xc06f1dd4), 0xe51ff004);
    assert.equal(word(0xc06f1dd8), hookAddress + 16);
    const oal = listModules(image).find(module => module.name === 'nk.exe');
    assert.equal(readU32(patched, oal.sections[2].dataOffset + 0xf0), 0x60000);
    assert.equal(readU32(patched, oal.sections[2].dataOffset + 0x100), hookAddress + 12);
    console.log(`A7300 display patch: hook at ${hookAddress.toString(16)} (${display.length} bytes), driver branches and trampolines verified`);
}
console.log(`A7300 main ROM: ${records} expected-byte records applied; embedded bstart and Launch50 verified; ${result.spans.length} skip spans zeroed first`);
console.log(`Original ${bytes.length} bytes; prepared ${patched.length} bytes`);
