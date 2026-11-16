import { readFileSync } from 'node:fs';
import { bytesEqual, hex, readU32 } from '../lib/bytes.mjs';
import { imageFromDumpOrSlot, listCopyEntries, listFiles, listModules, listUsedRegions, openImage, readFileContents, romHeaderFieldNames } from '../lib/rom-image.mjs';

const usage = `Usage: node tools/nk/compare-images.mjs SOURCE BUILT [--slot A|B]

Compares two CE images by content, ignoring layout: module records (o32 data pointers aside), section bytes, files, copy entries and the ROM header fields that do not depend on where things were placed. Exits 1 if anything else differs.`;

const argumentsList = process.argv.slice(2);
const slotIndex = argumentsList.indexOf('--slot');
const slot = slotIndex >= 0 ? argumentsList[slotIndex + 1] : undefined;
const paths = argumentsList.filter((argument, index) => !argument.startsWith('--') && (slotIndex < 0 || index !== slotIndex + 1));
if (paths.length !== 2) {
    console.log(usage);
    process.exit(1);
}

const open = (path) => openImage(imageFromDumpOrSlot(new Uint8Array(readFileSync(path)), slot).bytes);
const source = open(paths[0]);
const built = open(paths[1]);
const differences = [];
const differ = (message) => differences.push(message);

const layoutFields = new Set(['physlast', 'nummods', 'numfiles', 'ulCopyOffset', 'dllfirst', 'dlllast']);
for (const field of romHeaderFieldNames()) {
    if (layoutFields.has(field)) continue;
    if (source.header[field] !== built.header[field]) differ(`ROM header ${field}: ${hex(source.header[field])} -> ${hex(built.header[field])}`);
}
for (const field of ['dllfirst', 'dlllast']) {
    if (source.header[field] !== built.header[field]) console.log(`ROM header ${field}: ${hex(source.header[field])} -> ${hex(built.header[field])}`);
}

const sectionBytes = (image, section) => {
    if (section.dataOffset === null) return new Uint8Array(0);
    const length = section.virtualSize ? Math.min(section.physicalSize, section.virtualSize) : section.physicalSize;
    return image.bytes.subarray(section.dataOffset, section.dataOffset + length);
};

const byName = (list) => new Map(list.map((item) => [item.name.toLowerCase(), item]));
const sourceModules = byName(listModules(source));
const builtModules = byName(listModules(built));
for (const [name, module] of sourceModules) {
    const other = builtModules.get(name);
    if (!other) {
        console.log(`removed module ${module.name}`);
        continue;
    }
    const e32 = (image, entry) => image.bytes.subarray(entry.e32Offset, entry.e32Offset + 110);
    if (!bytesEqual(e32(source, module), e32(built, other))) differ(`${module.name}: e32 differs`);
    for (const field of ['attributes', 'time', 'fileSize']) {
        if (module[field] !== other[field]) differ(`${module.name}: TOC ${field} ${module[field]} -> ${other[field]}`);
    }
    if (module.sections.length !== other.sections.length) {
        differ(`${module.name}: ${module.sections.length} sections -> ${other.sections.length}`);
        continue;
    }
    module.sections.forEach((section, index) => {
        const peer = other.sections[index];
        for (const field of ['virtualSize', 'rva', 'physicalSize', 'realAddress', 'flags']) {
            if (section[field] !== peer[field]) differ(`${module.name} section ${index}: ${field} ${hex(section[field])} -> ${hex(peer[field])}`);
        }
        const first = sectionBytes(source, section);
        const second = sectionBytes(built, peer).slice();
        for (let offset = 0; offset + 4 <= first.length && offset + 4 <= second.length; offset += 4) {
            if (readU32(first, offset) === source.romHeaderVa && readU32(second, offset) === built.romHeaderVa) second.set(first.subarray(offset, offset + 4), offset);
        }
        if (!bytesEqual(first, second)) {
            const at = first.findIndex((byte, offset) => byte !== second[offset]);
            differ(`${module.name} section ${index}: contents differ from rva ${hex(section.rva + at)}`);
        }
    });
}
for (const [name, module] of builtModules) if (!sourceModules.has(name)) console.log(`added module ${module.name} at ${hex(module.virtualBase)}`);

const sourceFiles = byName(listFiles(source));
const builtFiles = byName(listFiles(built));
for (const [name, file] of sourceFiles) {
    const other = builtFiles.get(name);
    if (!other) {
        console.log(`removed file ${file.name}`);
        continue;
    }
    if (file.attributes !== other.attributes || file.time !== other.time) differ(`${file.name}: attributes or time differ`);
    if (!bytesEqual(readFileContents(source, file), readFileContents(built, other))) console.log(`changed file ${file.name}`);
}
for (const [name, file] of builtFiles) if (!sourceFiles.has(name)) console.log(`added file ${file.name}`);

const sourceCopies = listCopyEntries(source);
const builtCopies = listCopyEntries(built);
if (sourceCopies.length !== builtCopies.length) differ(`${sourceCopies.length} copy entries -> ${builtCopies.length}`);
sourceCopies.forEach((entry, index) => {
    const other = builtCopies[index];
    if (!other) return;
    if (entry.destination !== other.destination || entry.copyLength !== other.copyLength || entry.destinationLength !== other.destinationLength) differ(`copy entry ${index} differs`);
    const contents = (image, copy) => image.bytes.subarray(copy.sourceOffset, copy.sourceOffset + copy.copyLength);
    if (!bytesEqual(contents(source, entry), contents(built, other))) differ(`copy entry ${index} contents differ`);
});

const tocPointer = (image) => readU32(image.bytes, 0x44);
if (tocPointer(built) !== built.romHeaderVa) differ('image header pTOC does not point at the ROM header');

const occupied = [
    ...listUsedRegions(built).filter((region) => !/ section \d+$|^copy source /.test(region.name)),
    ...listModules(built).flatMap((module) => module.sections
        .filter((section) => section.dataOffset !== null)
        .map((section, index) => ({ name: `${module.name} section ${index}`, start: section.dataOffset, end: section.dataOffset + sectionBytes(built, section).length }))),
].filter((region) => region.end > region.start).sort((first, second) => first.start - second.start);
const seen = new Set();
for (let index = 1; index < occupied.length; index++) {
    const previous = occupied.slice(0, index).reduce((latest, region) => (region.end > latest.end ? region : latest));
    const region = occupied[index];
    const key = `${previous.name}|${region.name}`;
    if (region.start < previous.end && !seen.has(key) && !(previous.start === region.start && previous.end === region.end && previous.name === region.name)) {
        seen.add(key);
        differ(`${region.name} at ${hex(region.start)} overlaps ${previous.name} (${hex(previous.start)}-${hex(previous.end)})`);
    }
}

for (const difference of differences) console.log(`DIFFERS: ${difference}`);
console.log(differences.length ? `${differences.length} unexpected differences` : 'no unexpected differences');
process.exit(differences.length ? 1 : 0);
