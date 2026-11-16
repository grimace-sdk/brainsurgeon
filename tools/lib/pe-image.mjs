import { check, hex, latin1, readU16, readU32, writeU32 } from './bytes.mjs';

export const sectionWritable = 0x80000000;
export const sectionDiscardable = 0x02000000;
export const relocationsStrippedFlag = 0x0001;
export const dllFlag = 0x2000;

const absoluteRelocation = 0;
const highLowRelocation = 3;

export function parsePe(bytes, label = 'PE') {
    check(bytes.length > 0x40 && latin1(bytes, 0, 2) === 'MZ', `${label}: not an MZ executable`);
    const header = readU32(bytes, 0x3c);
    check(latin1(bytes, header, header + 4) === 'PE\0\0', `${label}: no PE signature`);
    const fileHeader = header + 4;
    const sectionCount = readU16(bytes, fileHeader + 2);
    const optionalSize = readU16(bytes, fileHeader + 16);
    const optional = fileHeader + 20;
    check(readU16(bytes, optional) === 0x10b, `${label}: not a PE32 image`);
    check(readU32(bytes, optional + 32) === 0x1000, `${label}: section alignment is not 4 KB`);
    const directoryCount = readU32(bytes, optional + 92);
    const directories = Array.from({ length: 16 }, (_, index) => index < directoryCount
        ? { rva: readU32(bytes, optional + 96 + index * 8), size: readU32(bytes, optional + 100 + index * 8) }
        : { rva: 0, size: 0 });
    const sections = Array.from({ length: sectionCount }, (_, index) => {
        const entry = optional + optionalSize + index * 40;
        const rawSize = readU32(bytes, entry + 16);
        const rawPointer = readU32(bytes, entry + 20);
        check(rawPointer + rawSize <= bytes.length, `${label}: section ${index} runs past the end of the file`);
        return {
            name: latin1(bytes, entry, entry + 8).replace(/\0.*$/, ''),
            virtualSize: readU32(bytes, entry + 8),
            rva: readU32(bytes, entry + 12),
            rawSize,
            flags: readU32(bytes, entry + 36),
            data: bytes.slice(rawPointer, rawPointer + rawSize),
        };
    });
    return {
        label,
        machine: readU16(bytes, fileHeader),
        timestamp: readU32(bytes, fileHeader + 4),
        characteristics: readU16(bytes, fileHeader + 18),
        entryRva: readU32(bytes, optional + 16),
        imageBase: readU32(bytes, optional + 28),
        subsystemMajor: readU16(bytes, optional + 48),
        subsystemMinor: readU16(bytes, optional + 50),
        sizeOfImage: readU32(bytes, optional + 56),
        subsystem: readU16(bytes, optional + 68),
        stackReserve: readU32(bytes, optional + 72),
        directories,
        sections,
    };
}

const sectionAt = (pe, rva) => pe.sections.find((section) => rva >= section.rva && rva < section.rva + Math.max(section.virtualSize, section.rawSize));

export function readRelocations(pe) {
    const { rva, size } = pe.directories[5];
    const relocations = [];
    if (!size) return relocations;
    const section = sectionAt(pe, rva);
    check(section, `${pe.label}: relocation directory is outside every section`);
    const table = section.data.subarray(rva - section.rva, rva - section.rva + size);
    for (let offset = 0; offset + 8 <= table.length;) {
        const page = readU32(table, offset);
        const blockSize = readU32(table, offset + 4);
        check(blockSize >= 8 && offset + blockSize <= table.length, `${pe.label}: bad relocation block at ${hex(rva + offset)}`);
        for (let entry = offset + 8; entry < offset + blockSize; entry += 2) {
            const value = readU16(table, entry);
            const type = value >> 12;
            if (type === absoluteRelocation) continue;
            check(type === highLowRelocation, `${pe.label}: relocation type ${type} at ${hex(page + (value & 0xfff))} is not supported`);
            relocations.push(page + (value & 0xfff));
        }
        offset += blockSize;
    }
    return relocations;
}

export function rebasePe(pe, newBase) {
    const delta = (newBase - pe.imageBase) >>> 0;
    const relocations = readRelocations(pe);
    check(delta === 0 || relocations.length || !(pe.characteristics & relocationsStrippedFlag), `${pe.label}: relocations are stripped, so it cannot move to ${hex(newBase)}`);
    if (delta) {
        for (const rva of relocations) {
            const section = sectionAt(pe, rva);
            check(section && rva + 4 <= section.rva + section.data.length, `${pe.label}: relocation at ${hex(rva)} is outside the section data`);
            const offset = rva - section.rva;
            writeU32(section.data, offset, (readU32(section.data, offset) + delta) >>> 0);
        }
    }
    return { ...pe, imageBase: newBase, linkedBase: pe.imageBase, relocationCount: delta ? relocations.length : 0 };
}
