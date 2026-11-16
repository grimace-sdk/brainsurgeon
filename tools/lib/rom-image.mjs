import { check, crc32, hex, indexOfByte, latin1, readU16, readU32 } from './bytes.mjs';
import { decompressRomLzx } from './lzx.mjs';

export { hex };

export const sectorSize = 512;
export const imageBase = 0x80200000;
export const romHeaderSize = 84;
export const tocEntrySize = 32;
export const filesEntrySize = 28;
export const copyEntrySize = 16;
export const o32EntrySize = 24;
export const trailerSignature = '06732754';
export const compressedFileAttribute = 0x800;
export const compressedSectionFlag = 0x2000;
export const identityLimit = 65536;

const slotOffsetsInSectors = { A: 0, B: 0x10000 };
const trailerOffsetInSectors = 30 * 1024 * 1024 / sectorSize - 1;

export function isImage(bytes) {
    return bytes.length > 0x44 && latin1(bytes, 0x40, 0x44) === 'ECEC';
}

export function readPartitionStart(dump) {
    check(dump.length >= 512 && readU16(dump, 0x1fe) === 0xaa55, 'Missing MBR signature');
    const entry = 0x1be + 2 * 16;
    check(dump[entry + 4] === 0x10, 'MBR partition 3 is not type 0x10');
    return readU32(dump, entry + 8);
}

export function readSlotTrailer(dump, slot) {
    const slotOffset = slotOffsetsInSectors[slot.toUpperCase()];
    check(slotOffset !== undefined, `Unknown slot ${slot}; expected A or B`);
    const startSector = readPartitionStart(dump) + slotOffset;
    const trailerSector = startSector + trailerOffsetInSectors;
    const trailerEnd = (trailerSector + 1) * sectorSize;
    check(trailerEnd <= dump.length, `Trailer sector ${hex(trailerSector)} is outside the dump`);
    const signature = latin1(dump, trailerEnd - 12, trailerEnd - 4);
    const length = readU32(dump, trailerEnd - 4);
    return { slot: slot.toUpperCase(), startSector, trailerSector, signature, length, valid: signature === trailerSignature };
}

export function readSlotImage(dump, slot) {
    const trailer = readSlotTrailer(dump, slot);
    check(trailer.valid, `Slot ${trailer.slot} trailer at sector ${hex(trailer.trailerSector)} is invalid`);
    const start = trailer.startSector * sectorSize;
    check(trailer.length > 0 && trailer.length <= 30 * 1024 * 1024 - sectorSize, `Slot ${trailer.slot} length ${hex(trailer.length)} is out of range`);
    check(start + trailer.length <= dump.length, 'Slot image runs past the end of the dump');
    return { trailer, bytes: dump.subarray(start, start + trailer.length) };
}

export function imageFromDumpOrSlot(contents, slot) {
    if (isImage(contents)) return { slot: null, bytes: contents };
    check(contents.length >= 512 && readU16(contents, 0x1fe) === 0xaa55, 'Not a Windows CE image (nk-slot-*.bin) or an eMMC dump');
    if (slot) return { slot: slot.toUpperCase(), bytes: readSlotImage(contents, slot).bytes };
    const valid = ['A', 'B'].find((candidate) => readSlotTrailer(contents, candidate).valid);
    check(valid, 'Not a CE image, and neither slot A nor slot B of this eMMC dump has a valid trailer');
    return { slot: valid, bytes: readSlotImage(contents, valid).bytes };
}

export function imageIdentity(bytes) {
    check(isImage(bytes), 'Missing ECEC signature: not a Windows CE ROM image');
    const offset = readU32(bytes, 0x44) - imageBase;
    check(offset >= 0 && offset + romHeaderSize <= bytes.length, 'ROM header is outside the image');
    const length = romHeaderSize + readU32(bytes, offset + 16) * tocEntrySize + readU32(bytes, offset + 48) * filesEntrySize;
    check(length <= identityLimit && offset + length <= bytes.length, `ROM header and tables (${length} bytes) are over ${identityLimit} bytes or run past the image`);
    return crc32(bytes.subarray(offset, offset + length));
}

export const identityHex = (identity) => identity.toString(16).padStart(8, '0');

export function openImage(bytes) {
    check(isImage(bytes), 'Missing ECEC signature: not a Windows CE ROM image');
    const image = { bytes, romHeaderVa: readU32(bytes, 0x44) };
    image.offsetOf = (va, length = 1) => vaToOffset(image, va, length);
    image.vaOf = (offset) => offsetToVa(image, offset);
    image.cString = (va) => {
        const start = image.offsetOf(va);
        const end = indexOfByte(bytes, 0, start);
        check(end >= 0, `Unterminated string at ${hex(va)}`);
        return latin1(bytes, start, end);
    };
    image.header = parseRomHeader(image);
    check(image.header.physfirst === imageBase, 'Image is not based at 0x80200000');
    return image;
}

export function vaToOffset(image, va, length = 1) {
    const offset = va - imageBase;
    check(offset >= 0 && offset + length <= image.bytes.length, `VA ${hex(va)} (+${length}) outside image`);
    return offset;
}

export function offsetToVa(image, offset) {
    check(offset >= 0 && offset <= image.bytes.length, `Offset ${hex(offset)} outside image`);
    return imageBase + offset;
}

const romHeaderFields = [
    ['dllfirst', 0, 4], ['dlllast', 4, 4], ['physfirst', 8, 4], ['physlast', 12, 4], ['nummods', 16, 4],
    ['ulRAMStart', 20, 4], ['ulRAMFree', 24, 4], ['ulRAMEnd', 28, 4], ['ulCopyEntries', 32, 4], ['ulCopyOffset', 36, 4],
    ['ulProfileLen', 40, 4], ['ulProfileOffset', 44, 4], ['numfiles', 48, 4], ['ulKernelFlags', 52, 4],
    ['ulFSRamPercent', 56, 4], ['ulDrivglobStart', 60, 4], ['ulDrivglobLen', 64, 4], ['usCPUType', 68, 2],
    ['usMiscFlags', 70, 2], ['pExtensions', 72, 4], ['ulTrackingStart', 76, 4], ['ulTrackingLen', 80, 4],
];

export function parseRomHeader(image) {
    const base = vaToOffset(image, image.romHeaderVa, romHeaderSize);
    const header = { va: image.romHeaderVa, offset: base };
    for (const [name, offset, size] of romHeaderFields) {
        header[name] = size === 2 ? readU16(image.bytes, base + offset) : readU32(image.bytes, base + offset);
    }
    return header;
}

export function romHeaderFieldNames() {
    return romHeaderFields.map(([name]) => name);
}

export function listCopyEntries(image) {
    const { ulCopyEntries, ulCopyOffset } = image.header;
    return Array.from({ length: ulCopyEntries }, (_, index) => {
        const va = ulCopyOffset + index * copyEntrySize;
        const entry = image.offsetOf(va, copyEntrySize);
        const source = readU32(image.bytes, entry);
        const copyLength = readU32(image.bytes, entry + 8);
        return {
            index, entryVa: va, entryOffset: entry, source, sourceOffset: image.offsetOf(source, copyLength || 1),
            destination: readU32(image.bytes, entry + 4), copyLength, destinationLength: readU32(image.bytes, entry + 12),
        };
    });
}

const fileTime = (bytes, offset) => {
    const low = readU32(bytes, offset);
    const high = readU32(bytes, offset + 4);
    const ticks = BigInt(high) << 32n | BigInt(low);
    const unixMilliseconds = Number(ticks / 10000n) - 11644473600000;
    return ticks === 0n ? null : new Date(unixMilliseconds).toISOString();
};

export function moduleTableVa(image) {
    return image.romHeaderVa + romHeaderSize;
}

export function fileTableVa(image) {
    return moduleTableVa(image) + image.header.nummods * tocEntrySize;
}

export function listModules(image) {
    const { bytes } = image;
    return Array.from({ length: image.header.nummods }, (_, index) => {
        const tocVa = moduleTableVa(image) + index * tocEntrySize;
        const toc = image.offsetOf(tocVa, tocEntrySize);
        const e32Va = readU32(bytes, toc + 20);
        const o32Va = readU32(bytes, toc + 24);
        const e32 = image.offsetOf(e32Va, 112);
        const objectCount = readU16(bytes, e32);
        const sections = Array.from({ length: objectCount }, (_, sectionIndex) => {
            const o32 = image.offsetOf(o32Va + sectionIndex * o32EntrySize, o32EntrySize);
            const dataPointer = readU32(bytes, o32 + 12);
            const physicalSize = readU32(bytes, o32 + 8);
            const flags = readU32(bytes, o32 + 20);
            return {
                virtualSize: readU32(bytes, o32), rva: readU32(bytes, o32 + 4), physicalSize, dataPointer,
                dataOffset: physicalSize ? image.offsetOf(dataPointer, physicalSize) : null,
                realAddress: readU32(bytes, o32 + 16), flags, compressed: Boolean(flags & compressedSectionFlag),
            };
        });
        return {
            index, tocVa, tocOffset: toc,
            name: image.cString(readU32(bytes, toc + 16)),
            attributes: readU32(bytes, toc), time: fileTime(bytes, toc + 4), fileSize: readU32(bytes, toc + 12),
            e32Va, e32Offset: e32, o32Va, o32Offset: image.offsetOf(o32Va, Math.max(objectCount, 1) * o32EntrySize),
            loadOffset: readU32(bytes, toc + 28),
            imageFlags: readU16(bytes, e32 + 2), entryRva: readU32(bytes, e32 + 4), virtualBase: readU32(bytes, e32 + 8),
            virtualSize: readU32(bytes, e32 + 20), subsystem: readU16(bytes, e32 + 108), sections,
        };
    });
}

export function listFiles(image) {
    const { bytes } = image;
    return Array.from({ length: image.header.numfiles }, (_, index) => {
        const entryVa = fileTableVa(image) + index * filesEntrySize;
        const entry = image.offsetOf(entryVa, filesEntrySize);
        const attributes = readU32(bytes, entry);
        const realSize = readU32(bytes, entry + 12);
        const compressedSize = readU32(bytes, entry + 16);
        const loadVa = readU32(bytes, entry + 24);
        return {
            index, entryVa, entryOffset: entry, name: image.cString(readU32(bytes, entry + 20)),
            attributes, time: fileTime(bytes, entry + 4), realSize, compressedSize,
            loadVa, loadOffset: image.offsetOf(loadVa, Math.max(compressedSize, 1)),
            compressed: Boolean(attributes & compressedFileAttribute),
        };
    });
}

export function findFile(image, name) {
    const file = listFiles(image).find((candidate) => candidate.name.toLowerCase() === name.toLowerCase());
    check(file, `${name} is not a ROM file`);
    return file;
}

export function findModule(image, name) {
    const module = listModules(image).find((candidate) => candidate.name.toLowerCase() === name.toLowerCase());
    check(module, `${name} is not a ROM module`);
    return module;
}

export function readStoredFile(image, file) {
    return image.bytes.subarray(file.loadOffset, file.loadOffset + file.compressedSize);
}

export function readFileContents(image, file) {
    const stored = readStoredFile(image, file);
    if (!file.compressed) {
        check(file.compressedSize === file.realSize, `${file.name} stored size differs from its size`);
        return Uint8Array.from(stored);
    }
    const expanded = decompressRomLzx(stored);
    check(expanded.length === file.realSize, `${file.name} decompressed to ${expanded.length}, expected ${file.realSize}`);
    return expanded;
}

export function freeGap(image) {
    const { physlast, ulRAMStart } = image.header;
    return { from: physlast, to: ulRAMStart, bytes: ulRAMStart - physlast };
}

export function listUsedRegions(image) {
    const regions = [
        { name: 'ROMHDR', start: image.header.offset, end: image.header.offset + romHeaderSize },
        { name: 'module TOC', start: image.offsetOf(moduleTableVa(image)), end: image.offsetOf(fileTableVa(image)) + image.header.numfiles * filesEntrySize },
    ];
    for (const copy of listCopyEntries(image)) {
        regions.push({ name: `copy entry ${copy.index}`, start: copy.entryOffset, end: copy.entryOffset + copyEntrySize });
        regions.push({ name: `copy source ${copy.index}`, start: copy.sourceOffset, end: copy.sourceOffset + copy.copyLength });
    }
    for (const module of listModules(image)) {
        const nameOffset = image.offsetOf(readU32(image.bytes, module.tocOffset + 16));
        regions.push({ name: `${module.name} e32`, start: module.e32Offset, end: module.e32Offset + 112 });
        regions.push({ name: `${module.name} o32`, start: module.o32Offset, end: module.o32Offset + module.sections.length * o32EntrySize });
        regions.push({ name: `${module.name} name`, start: nameOffset, end: nameOffset + module.name.length + 1 });
        module.sections.forEach((section, index) => {
            if (section.dataOffset !== null) regions.push({ name: `${module.name} section ${index}`, start: section.dataOffset, end: section.dataOffset + section.physicalSize });
        });
    }
    for (const file of listFiles(image)) {
        regions.push({ name: file.name, start: file.loadOffset, end: file.loadOffset + file.compressedSize });
        const nameOffset = image.offsetOf(readU32(image.bytes, file.entryOffset + 20));
        regions.push({ name: `${file.name} name`, start: nameOffset, end: nameOffset + file.name.length + 1 });
    }
    return regions.sort((first, second) => first.start - second.start || first.end - second.end);
}

export function slackAfter(image, offset) {
    const next = listUsedRegions(image).find((region) => region.start >= offset && region.end > region.start);
    const nextOffset = next ? next.start : image.bytes.length;
    const gap = image.bytes.subarray(offset, nextOffset);
    return { offset, nextOffset, nextName: next ? next.name : 'end of image', bytes: gap.length, allZero: gap.every((byte) => byte === 0) };
}
