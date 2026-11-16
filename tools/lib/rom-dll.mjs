import { check, encodeLatin1, hex, readU16, readU32, writeU32 } from './bytes.mjs';
import { findModule } from './rom-image.mjs';

const ldrLiteralMask = 0x0f7f0000;
const ldrLiteralValue = 0x051f0000;
const fileAlignment = 0x200;
const alignUp = (value, alignment) => Math.ceil(value / alignment) * alignment;

const writeU16 = (bytes, offset, value) => {
    bytes[offset] = value & 0xff;
    bytes[offset + 1] = (value >> 8) & 0xff;
};

function codeRangesFromPdata(memory, pdataRva, pdataSize, base) {
    const ranges = [];
    for (let offset = 0; offset + 8 <= pdataSize; offset += 8) {
        const begin = readU32(memory, pdataRva + offset);
        const flags = readU32(memory, pdataRva + offset + 4);
        const length = ((flags >>> 8) & 0x3fffff) * 4;
        if (begin < base || !length) continue;
        ranges.push({ start: begin - base, end: begin - base + length });
    }
    return ranges.sort((first, second) => first.start - second.start);
}

function insideRanges(ranges, rva) {
    let low = 0;
    let high = ranges.length - 1;
    while (low <= high) {
        const middle = (low + high) >> 1;
        if (rva < ranges[middle].start) high = middle - 1;
        else if (rva >= ranges[middle].end) low = middle + 1;
        else return true;
    }
    return false;
}

function recoverRelocations({ memory, base, sizeOfImage, sections, codeRanges, exclusions, pdata }) {
    const scanEnd = (section) => section.rva + Math.ceil(section.rawSize / 4) * 4;
    const sectionStarts = new Set(sections.map((section) => base + section.rva));
    const inImage = (value) => value >= base && value < base + sizeOfImage && ((value & 0xffff) !== 0 || sectionStarts.has(value));
    const excluded = (rva) => exclusions.some(({ start, end }) => rva >= start && rva < end);
    const literalTargets = new Set();
    for (const section of sections.filter((candidate) => candidate.code)) {
        for (let rva = section.rva; rva + 4 <= scanEnd(section); rva += 4) {
            if (!insideRanges(codeRanges, rva)) continue;
            const word = readU32(memory, rva);
            if ((word & ldrLiteralMask) !== ldrLiteralValue || word >>> 28 === 0xf) continue;
            const offset = word & 0xfff;
            const target = rva + 8 + (word & 0x00800000 ? offset : -offset);
            if (target % 4 === 0) literalTargets.add(target);
        }
    }
    const relocations = [];
    let pdataRelocations = 0;
    for (const section of sections) {
        for (let rva = section.rva; rva + 4 <= scanEnd(section); rva += 4) {
            if (excluded(rva)) continue;
            if (!inImage(readU32(memory, rva))) continue;
            if (pdata && rva >= pdata.start && rva < pdata.end) {
                if ((rva - pdata.start) % 8 === 0) {
                    relocations.push(rva);
                    pdataRelocations++;
                }
                continue;
            }
            if (section.code && insideRanges(codeRanges, rva)) {
                if (literalTargets.has(rva)) relocations.push(rva);
                continue;
            }
            relocations.push(rva);
        }
    }
    return { relocations: relocations.sort((first, second) => first - second), pdataRelocations };
}

function buildRelocationSection(relocations) {
    const pages = new Map();
    for (const rva of relocations) {
        const page = rva & ~0xfff;
        if (!pages.has(page)) pages.set(page, []);
        pages.get(page).push(rva & 0xfff);
    }
    const blocks = [...pages].sort((first, second) => first[0] - second[0]).map(([page, offsets]) => {
        const entries = offsets.length % 2 ? offsets.length + 1 : offsets.length;
        const block = new Uint8Array(8 + entries * 2);
        writeU32(block, 0, page);
        writeU32(block, 4, block.length);
        offsets.forEach((offset, index) => writeU16(block, 8 + index * 2, 0x3000 | offset));
        return block;
    });
    const section = new Uint8Array(blocks.reduce((total, block) => total + block.length, 0));
    blocks.reduce((offset, block) => {
        section.set(block, offset);
        return offset + block.length;
    }, 0);
    return section;
}

export function extractRomDll(image, moduleName, { spareSections = 0 } = {}) {
    const { bytes } = image;
    const module = findModule(image, moduleName);
    const e32 = module.e32Offset;
    const imageFlags = readU16(bytes, e32 + 2);
    const entryRva = readU32(bytes, e32 + 4);
    const virtualBase = readU32(bytes, e32 + 8);
    const subsystemMajor = readU16(bytes, e32 + 12);
    const subsystemMinor = readU16(bytes, e32 + 14);
    const stackMax = readU32(bytes, e32 + 16);
    const virtualSize = readU32(bytes, e32 + 20);
    const timestamp = readU32(bytes, e32 + 32);
    const units = Array.from({ length: 9 }, (_, unit) => ({ rva: readU32(bytes, e32 + 36 + unit * 8), size: readU32(bytes, e32 + 40 + unit * 8) }));
    const subsystem = readU16(bytes, e32 + 108);

    const sections = module.sections.map((section, index) => {
        check(!section.compressed, `${moduleName} section ${index} is compressed`);
        return {
            virtualSize: section.virtualSize,
            rva: section.rva,
            rawSize: section.physicalSize,
            dataPointer: section.dataPointer,
            realAddress: section.realAddress,
            flags: section.flags,
            code: Boolean(section.flags & 0x20000020),
            moved: section.realAddress !== virtualBase + section.rva,
        };
    });

    const relocRva = alignUp(virtualSize, 0x1000);
    const memory = new Uint8Array(relocRva);
    for (const section of sections) {
        if (!section.rawSize) continue;
        const start = image.offsetOf(section.dataPointer, section.rawSize);
        const length = Math.min(section.rawSize, memory.length - section.rva);
        memory.set(bytes.subarray(start, start + length), section.rva);
    }

    const importDescriptors = () => {
        const descriptors = [];
        for (let descriptor = units[1].rva; units[1].rva && descriptor + 20 <= units[1].rva + units[1].size; descriptor += 20) {
            if (!readU32(memory, descriptor + 12)) break;
            descriptors.push({ lookup: readU32(memory, descriptor), firstThunk: readU32(memory, descriptor + 16) });
        }
        return descriptors;
    };
    for (const { lookup, firstThunk } of importDescriptors()) {
        if (!lookup || lookup === firstThunk) continue;
        for (let slot = 0; readU32(memory, lookup + slot * 4); slot++) writeU32(memory, firstThunk + slot * 4, readU32(memory, lookup + slot * 4));
    }

    const movedSections = sections.filter((section) => section.moved);
    if (movedSections.length) {
        for (const section of sections) {
            for (let rva = section.rva; rva + 4 <= section.rva + section.rawSize; rva += 4) {
                const value = readU32(memory, rva);
                const moved = movedSections.find((candidate) => value >= candidate.realAddress && value < candidate.realAddress + candidate.virtualSize);
                if (moved) writeU32(memory, rva, virtualBase + moved.rva + (value - moved.realAddress));
            }
        }
    }

    const exclusions = [units[0], units[1], units[2], units[6]].filter((unit) => unit.rva && unit.size).map((unit) => ({ start: unit.rva, end: unit.rva + unit.size }));
    for (const { lookup, firstThunk } of importDescriptors()) {
        for (const table of [lookup, firstThunk].filter(Boolean)) {
            let end = table;
            while (end + 4 <= memory.length && readU32(memory, end)) end += 4;
            exclusions.push({ start: table, end: end + 4 });
        }
    }
    const pdata = units[3].rva && units[3].size ? { start: units[3].rva, end: units[3].rva + units[3].size } : null;
    const codeRanges = pdata ? codeRangesFromPdata(memory, pdata.start, pdata.end - pdata.start, virtualBase) : [];
    const { relocations, pdataRelocations } = recoverRelocations({
        memory, base: virtualBase, sizeOfImage: virtualSize,
        sections: sections.map((section) => ({ rva: section.rva, rawSize: section.rawSize, code: section.code })), codeRanges, exclusions, pdata,
    });
    check(pdataRelocations === (pdata ? (pdata.end - pdata.start) / 8 : 0), `${moduleName}: not every .pdata entry was relocated`);

    const relocSection = buildRelocationSection(relocations);
    const sectionName = (section) => {
        if (section.rva === units[2].rva) return '.rsrc';
        if (section.rva === units[3].rva) return '.pdata';
        if (section.flags & 0x20) return '.text';
        if (section.flags & 0x80000000) return '.data';
        return '.rdata';
    };
    const allSections = [
        ...sections.map((section) => ({ ...section, name: sectionName(section), data: memory.subarray(section.rva, section.rva + section.rawSize) })),
        { name: '.reloc', rva: relocRva, virtualSize: relocSection.length, rawSize: relocSection.length, flags: 0x42000040, data: relocSection },
    ];
    const headerSize = alignUp(0x40 + 4 + 20 + 224 + (allSections.length + spareSections) * 40, fileAlignment);
    let filePointer = headerSize;
    for (const section of allSections) {
        section.fileSize = alignUp(section.data.length, fileAlignment);
        section.filePointer = section.fileSize ? filePointer : 0;
        filePointer += section.fileSize;
    }

    const pe = new Uint8Array(filePointer);
    pe.set(encodeLatin1('MZ'), 0);
    writeU32(pe, 0x3c, 0x40);
    pe.set(encodeLatin1('PE\0\0'), 0x40);
    const fileHeader = 0x44;
    writeU16(pe, fileHeader, image.header.usCPUType);
    writeU16(pe, fileHeader + 2, allSections.length);
    writeU32(pe, fileHeader + 4, timestamp);
    writeU16(pe, fileHeader + 16, 224);
    writeU16(pe, fileHeader + 18, (imageFlags & ~0x0001) | 0x0102);
    const optional = fileHeader + 20;
    writeU16(pe, optional, 0x10b);
    writeU32(pe, optional + 4, allSections.filter((section) => section.flags & 0x20).reduce((sum, section) => sum + section.fileSize, 0));
    writeU32(pe, optional + 8, allSections.filter((section) => !(section.flags & 0x20)).reduce((sum, section) => sum + section.fileSize, 0));
    writeU32(pe, optional + 16, entryRva);
    writeU32(pe, optional + 20, 0x1000);
    writeU32(pe, optional + 28, virtualBase);
    writeU32(pe, optional + 32, 0x1000);
    writeU32(pe, optional + 36, fileAlignment);
    writeU16(pe, optional + 40, 4);
    writeU16(pe, optional + 48, subsystemMajor);
    writeU16(pe, optional + 50, subsystemMinor);
    writeU32(pe, optional + 56, alignUp(relocRva + relocSection.length, 0x1000));
    writeU32(pe, optional + 60, headerSize);
    writeU16(pe, optional + 68, subsystem);
    writeU32(pe, optional + 72, stackMax);
    writeU32(pe, optional + 76, 0x1000);
    writeU32(pe, optional + 80, 0x100000);
    writeU32(pe, optional + 84, 0x1000);
    writeU32(pe, optional + 92, 16);
    [0, 1, 2, 3, 4, 6].forEach((unit) => {
        writeU32(pe, optional + 96 + unit * 8, units[unit].rva);
        writeU32(pe, optional + 100 + unit * 8, units[unit].size);
    });
    writeU32(pe, optional + 96 + 5 * 8, relocRva);
    writeU32(pe, optional + 100 + 5 * 8, relocSection.length);
    allSections.forEach((section, index) => {
        const header = optional + 224 + index * 40;
        pe.set(encodeLatin1(section.name), header);
        writeU32(pe, header + 8, section.virtualSize);
        writeU32(pe, header + 12, section.rva);
        writeU32(pe, header + 16, section.fileSize);
        writeU32(pe, header + 20, section.filePointer);
        writeU32(pe, header + 36, (section.flags & ~0x2000) >>> 0);
        pe.set(section.data, section.filePointer);
    });
    return pe;
}

export function patchBytes(bytes, patches, label) {
    const patched = bytes.slice();
    for (const { offset, original, replacement } of patches) {
        const start = Number.parseInt(offset, 16);
        const expected = Uint8Array.from(original.match(/../g), (pair) => Number.parseInt(pair, 16));
        const replacementBytes = Uint8Array.from(replacement.match(/../g), (pair) => Number.parseInt(pair, 16));
        check(expected.length === replacementBytes.length, `${label} ${hex(start)}: original and replacement differ in length`);
        check(start + expected.length <= patched.length && expected.every((byte, index) => patched[start + index] === byte),
            `${label} ${hex(start)}: expected ${original}`);
        patched.set(replacementBytes, start);
    }
    return patched;
}
