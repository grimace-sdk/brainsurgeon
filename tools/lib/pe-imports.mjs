import { check, encodeLatin1, hex, indexOfByte, latin1, readU16, readU32, writeU32 } from './bytes.mjs';

const alignUp = (value, alignment) => Math.ceil(value / alignment) * alignment;

const writeU16 = (bytes, offset, value) => {
    bytes[offset] = value & 0xff;
    bytes[offset + 1] = (value >> 8) & 0xff;
};

export function redirectImports(original, fromDll, toDll, mapping) {
    const redirects = new Map(Object.entries(mapping).map(([source, name]) => [/^\d+$/.test(source) ? Number(source) : source, name]));
    check(latin1(original, 0, 2) === 'MZ', 'Not an MZ executable');
    const peOffset = readU32(original, 0x3c);
    check(latin1(original, peOffset, peOffset + 4) === 'PE\0\0', 'Missing PE signature');
    const fileHeader = peOffset + 4;
    const sectionCount = readU16(original, fileHeader + 2);
    const optionalSize = readU16(original, fileHeader + 16);
    const optional = fileHeader + 20;
    check(readU16(original, optional) === 0x10b, 'Expected a PE32 optional header');
    const sectionAlignment = readU32(original, optional + 32);
    const fileAlignment = readU32(original, optional + 36);
    const sizeOfHeaders = readU32(original, optional + 60);
    const importDirectory = optional + 96 + 8;
    const boundDirectory = optional + 96 + 8 * 11;
    const sectionTable = optional + optionalSize;

    const sections = Array.from({ length: sectionCount }, (_, index) => {
        const header = sectionTable + index * 40;
        return {
            virtualSize: readU32(original, header + 8),
            virtualAddress: readU32(original, header + 12),
            rawSize: readU32(original, header + 16),
            rawPointer: readU32(original, header + 20),
        };
    });
    const fileOffset = (rva) => {
        const section = sections.find((candidate) => rva >= candidate.virtualAddress && rva < candidate.virtualAddress + Math.max(candidate.virtualSize, candidate.rawSize));
        check(section, `RVA ${hex(rva)} is not inside a section`);
        return rva - section.virtualAddress + section.rawPointer;
    };
    const readString = (rva) => {
        const start = fileOffset(rva);
        return latin1(original, start, indexOfByte(original, 0, start));
    };

    check(readU32(original, boundDirectory) === 0, 'Bound imports are not supported');
    const descriptors = [];
    for (let offset = fileOffset(readU32(original, importDirectory)); ; offset += 20) {
        const descriptor = {
            lookup: readU32(original, offset),
            timestamp: readU32(original, offset + 4),
            forwarder: readU32(original, offset + 8),
            name: readU32(original, offset + 12),
            firstThunk: readU32(original, offset + 16),
        };
        if (!descriptor.name) break;
        descriptors.push(descriptor);
    }
    const source = descriptors.find((descriptor) => readString(descriptor.name).toLowerCase() === fromDll.toLowerCase());
    check(source, `${fromDll} is not imported`);

    const lookupEntries = [];
    for (let rva = source.lookup || source.firstThunk; ; rva += 4) {
        const entry = readU32(original, fileOffset(rva));
        if (!entry) break;
        lookupEntries.push(entry);
    }
    const importKey = (entry) => (entry & 0x80000000 ? entry & 0xffff : readString(entry + 2));
    const redirectedSlots = [];
    lookupEntries.forEach((entry, index) => {
        const key = importKey(entry);
        if (redirects.has(key)) redirectedSlots.push({ index, ordinal: key, name: redirects.get(key) });
    });
    check(redirectedSlots.length === new Set(redirectedSlots.map((slot) => slot.ordinal)).size, 'Import listed twice');
    for (const key of redirects.keys()) check(redirectedSlots.some((slot) => slot.ordinal === key), `${key} is not imported from ${fromDll}`);
    const substitute = lookupEntries.find((entry) => !redirects.has(importKey(entry)));
    check(substitute, 'No remaining import to stand in for redirected slots');

    const newHeaderEnd = sectionTable + (sectionCount + 1) * 40;
    check(newHeaderEnd <= sizeOfHeaders, 'No room for another section header');
    check(newHeaderEnd <= Math.min(...sections.map((section) => section.rawPointer).filter(Boolean)), 'Section header would overlap section data');

    const last = sections.reduce((first, second) => (first.virtualAddress > second.virtualAddress ? first : second));
    const sectionRva = alignUp(last.virtualAddress + Math.max(last.virtualSize, last.rawSize), sectionAlignment);
    const descriptorsSize = (descriptors.length + redirectedSlots.length + 1) * 20;
    const sourceLookupRva = sectionRva + descriptorsSize;
    const redirectLookupRva = sourceLookupRva + (lookupEntries.length + 1) * 4;
    let stringsRva = redirectLookupRva + redirectedSlots.length * 8;
    const strings = [];
    const addString = (bytes) => {
        const rva = stringsRva;
        const padded = new Uint8Array(bytes.length + (bytes.length % 2));
        padded.set(bytes);
        strings.push(padded);
        stringsRva += padded.length;
        return rva;
    };
    const targetNameRva = addString(encodeLatin1(`${toDll}\0`));
    for (const slot of redirectedSlots) {
        const hintName = new Uint8Array(2 + slot.name.length + 1);
        hintName.set(encodeLatin1(slot.name), 2);
        slot.hintNameRva = addString(hintName);
    }
    const sectionSize = stringsRva - sectionRva;
    const section = new Uint8Array(alignUp(sectionSize, fileAlignment));

    let cursor = 0;
    const writeDescriptor = (descriptor) => {
        writeU32(section, cursor, descriptor.lookup);
        writeU32(section, cursor + 4, descriptor.timestamp);
        writeU32(section, cursor + 8, descriptor.forwarder);
        writeU32(section, cursor + 12, descriptor.name);
        writeU32(section, cursor + 16, descriptor.firstThunk);
        cursor += 20;
    };
    for (const descriptor of descriptors) writeDescriptor(descriptor === source ? { ...descriptor, lookup: sourceLookupRva } : descriptor);
    redirectedSlots.forEach((slot, index) => writeDescriptor({
        lookup: redirectLookupRva + index * 8, timestamp: 0, forwarder: 0,
        name: targetNameRva, firstThunk: source.firstThunk + slot.index * 4,
    }));
    lookupEntries.forEach((entry, index) => {
        const redirected = redirectedSlots.some((slot) => slot.index === index);
        writeU32(section, sourceLookupRva - sectionRva + index * 4, (redirected ? substitute : entry) >>> 0);
    });
    redirectedSlots.forEach((slot, index) => writeU32(section, redirectLookupRva - sectionRva + index * 8, slot.hintNameRva));
    strings.reduce((offset, bytes) => {
        section.set(bytes, offset);
        return offset + bytes.length;
    }, targetNameRva - sectionRva);

    const rawPointer = alignUp(original.length, fileAlignment);
    const patched = new Uint8Array(rawPointer + section.length);
    patched.set(original);
    patched.set(section, rawPointer);
    const newHeader = sectionTable + sectionCount * 40;
    patched.fill(0, newHeader, newHeader + 40);
    patched.set(encodeLatin1('.brnimp'), newHeader);
    writeU32(patched, newHeader + 8, sectionSize);
    writeU32(patched, newHeader + 12, sectionRva);
    writeU32(patched, newHeader + 16, section.length);
    writeU32(patched, newHeader + 20, rawPointer);
    writeU32(patched, newHeader + 36, 0x40000040);
    writeU16(patched, fileHeader + 2, sectionCount + 1);
    writeU32(patched, optional + 56, alignUp(sectionRva + sectionSize, sectionAlignment));
    writeU32(patched, importDirectory, sectionRva);
    writeU32(patched, importDirectory + 4, descriptorsSize);
    return patched;
}
