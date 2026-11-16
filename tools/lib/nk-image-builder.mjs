import { check, crc32, encodeLatin1, hex, readU32, writeU32 } from './bytes.mjs';
import { makePatchRecords, formatPatch } from './nk-patch.mjs';
import { applyRegFile, readRegistry, writeRegistry } from './registry-edit.mjs';
import { copyEntrySize, filesEntrySize, imageBase, listCopyEntries, listFiles, listModules, openImage, readFileContents, readStoredFile, romHeaderSize, tocEntrySize } from './rom-image.mjs';
import { dllFlag, parsePe, rebasePe, relocationsStrippedFlag, sectionDiscardable, sectionWritable } from './pe-image.mjs';

const pageSize = 0x1000;
const e32RecordSize = 112;
const e32Size = 110;
const o32RecordSize = 24;
const headerSignatureOffset = 0x40;
const kernelSpaceStart = 0xc0000000;
const physicalSpaceStart = 0x80000000;
const exeBase = 0x10000;
const moduleAttributes = 0x7;
const resourceUnit = 2;

const align = (value, alignment) => Math.ceil(value / alignment) * alignment;
const align64k = (value) => align(value, 0x10000);
const lower = (name) => name.toLowerCase();

const unixToFileTime = (seconds) => {
    const ticks = (BigInt(seconds) + 11644473600n) * 10000000n;
    const bytes = new Uint8Array(8);
    writeU32(bytes, 0, Number(ticks & 0xffffffffn));
    writeU32(bytes, 4, Number(ticks >> 32n));
    return bytes;
};

class Layout {
    constructor(capacity) {
        this.bytes = new Uint8Array(capacity);
        this.cursor = 0;
        this.holes = [];
    }

    grow(end, label) {
        check(end <= this.bytes.length, `${label} does not fit: the image would pass ulRAMStart`);
        this.cursor = Math.max(this.cursor, end);
    }

    placePages(data, reservedLength, label) {
        const unaligned = align(this.cursor, 4);
        const start = align(this.cursor, pageSize);
        if (start > unaligned) this.holes.push({ start: unaligned, end: start });
        this.grow(start + Math.max(data.length, reservedLength), label);
        this.bytes.set(data, start);
        return start;
    }

    placePacked(data, label) {
        const length = align(data.length, 4);
        const hole = this.holes.find((candidate) => candidate.end - candidate.start >= length);
        let start;
        if (hole) {
            start = hole.start;
            hole.start += length;
        } else {
            start = align(this.cursor, 4);
            this.grow(start + length, label);
        }
        this.bytes.set(data, start);
        return start;
    }
}

function sharpModules(image) {
    const { bytes } = image;
    return listModules(image).map((module) => ({
        name: module.name,
        attributes: module.attributes,
        time: bytes.slice(module.tocOffset + 4, module.tocOffset + 12),
        fileSize: module.fileSize,
        loadOffset: module.loadOffset,
        e32: bytes.slice(module.e32Offset, module.e32Offset + e32Size),
        imageFlags: module.imageFlags,
        virtualBase: module.virtualBase,
        virtualSize: module.virtualSize,
        sections: module.sections.map((section) => ({
            virtualSize: section.virtualSize,
            rva: section.rva,
            physicalSize: section.physicalSize,
            dataPointer: section.dataPointer,
            realAddress: section.realAddress,
            flags: section.flags,
            sourceOffset: section.dataOffset,
        })),
    }));
}

function sharpFiles(image) {
    const { bytes } = image;
    return listFiles(image).map((file) => ({
        name: file.name,
        entry: bytes.slice(file.entryOffset, file.entryOffset + filesEntrySize),
        stored: readStoredFile(image, file),
        file,
    }));
}

function fixedSections(modules) {
    const runsInPlace = (module) => module.sections.some((section) => section.physicalSize && section.dataPointer === section.realAddress &&
        section.realAddress >= imageBase && section.realAddress < kernelSpaceStart);
    const kernelModules = modules.filter(runsInPlace);
    check(kernelModules.length, 'No module runs in place; cannot find the kernel');
    const sections = kernelModules.flatMap((module) => module.sections)
        .filter((section) => section.physicalSize && !(section.flags & sectionWritable))
        .sort((first, second) => first.sourceOffset - second.sourceOffset);
    check(sections[0].sourceOffset >= pageSize, 'A kernel section overlaps the image header page');
    const holes = [];
    let previousEnd = pageSize;
    for (const section of sections) {
        if (section.sourceOffset > previousEnd) holes.push({ start: previousEnd, end: section.sourceOffset });
        previousEnd = Math.max(previousEnd, align(section.sourceOffset + storedLength(section), 4));
    }
    const end = align(previousEnd, pageSize);
    if (end > previousEnd) holes.push({ start: previousEnd, end });
    return { modules: new Set(kernelModules.map((module) => module.name)), sections: new Set(sections), end, holes };
}

function storedLength(section) {
    return section.virtualSize ? Math.min(section.physicalSize, section.virtualSize) : section.physicalSize;
}

function addressRanges(modules) {
    const ranges = { user: { bottom: Infinity, top: 0 }, kernel: { bottom: Infinity, top: 0 } };
    for (const module of modules) {
        if (!(module.imageFlags & dllFlag) || module.virtualBase >= physicalSpaceStart && module.virtualBase < kernelSpaceStart) continue;
        const range = module.virtualBase >= kernelSpaceStart ? ranges.kernel : ranges.user;
        range.bottom = Math.min(range.bottom, module.virtualBase);
        range.top = Math.max(range.top, align64k(module.virtualBase + module.virtualSize));
    }
    return ranges;
}

function newModule({ name, contents, kernel }, ranges) {
    let pe = parsePe(contents, name);
    check(pe.machine === 0x1c2 || pe.machine === 0x1c0, `${name}: not an ARM image (machine ${hex(pe.machine, 4)})`);
    const isDll = Boolean(pe.characteristics & dllFlag);
    check(isDll || !kernel, `${name}: only DLLs can be kernel modules`);
    let base;
    if (!isDll) {
        base = pe.characteristics & relocationsStrippedFlag ? pe.imageBase : exeBase;
    } else {
        const range = kernel ? ranges.kernel : ranges.user;
        base = range.top;
        range.top = align64k(base + pe.sizeOfImage + pageSize);
        range.bottom = Math.min(range.bottom, base);
    }
    pe = rebasePe(pe, base);
    const kept = pe.sections.filter((section) => !(section.flags & sectionDiscardable) || section.rva === pe.directories[resourceUnit].rva);
    const sections = kept.map((section) => {
        let flags = section.flags & ~sectionDiscardable;
        if (section.rva === pe.directories[resourceUnit].rva) flags &= ~sectionWritable;
        const virtualSize = section.virtualSize || section.rawSize;
        const physicalSize = Math.min(section.rawSize, virtualSize);
        return {
            virtualSize, rva: section.rva, physicalSize, realAddress: (base + section.rva) >>> 0, flags: flags >>> 0,
            data: section.data.subarray(0, physicalSize),
        };
    });
    const e32 = new Uint8Array(e32Size);
    const view = new DataView(e32.buffer);
    view.setUint16(0, sections.length, true);
    view.setUint16(2, pe.characteristics, true);
    view.setUint32(4, pe.entryRva, true);
    view.setUint32(8, base, true);
    view.setUint16(12, pe.subsystemMajor, true);
    view.setUint16(14, pe.subsystemMinor, true);
    view.setUint32(16, pe.stackReserve, true);
    view.setUint32(20, pe.sizeOfImage, true);
    view.setUint32(24, pe.directories[14].rva, true);
    view.setUint32(28, pe.directories[14].size, true);
    view.setUint32(32, pe.timestamp, true);
    for (let unit = 0; unit < 9; unit++) {
        view.setUint32(36 + unit * 8, pe.directories[unit].rva, true);
        view.setUint32(40 + unit * 8, pe.directories[unit].size, true);
    }
    view.setUint16(108, pe.subsystem, true);
    return {
        name, attributes: moduleAttributes, time: unixToFileTime(pe.timestamp), fileSize: contents.length, loadOffset: 0,
        e32, imageFlags: pe.characteristics, virtualBase: base, virtualSize: pe.sizeOfImage, sections, added: true,
        linkedBase: pe.linkedBase, relocationCount: pe.relocationCount, kernel: base >= kernelSpaceStart,
    };
}

function applyWordPatch(layout, modules, patch) {
    const module = modules.find((candidate) => lower(candidate.name) === lower(patch.module));
    check(module, `${patch.module} is not in the built image`);
    for (const { address, original, replacement, purpose } of patch.words) {
        check(address % 4 === 0, `${hex(address)} is not word aligned`);
        const section = module.sections.find((candidate) => candidate.newOffset !== undefined &&
            address >= candidate.realAddress && address + 4 <= candidate.realAddress + candidate.storedLength);
        check(section, `${hex(address)} (${purpose}) is outside ${patch.module}'s stored sections`);
        const offset = section.newOffset + address - section.realAddress;
        check(readU32(layout.bytes, offset) === original, `${hex(address)} (${purpose}) is ${hex(readU32(layout.bytes, offset))}, expected ${hex(original)}`);
        writeU32(layout.bytes, offset, replacement);
    }
}

export function buildNkImage({ bytes, remove = [], rename = [], modules: addedModules = [], files: addedFiles = [], registryAdditions = [], wordPatches = [] }) {
    const image = openImage(bytes);
    const header = image.header;
    const notes = [];

    const removals = new Set(remove.map(lower));
    const sourceModules = sharpModules(image);
    const sourceFiles = sharpFiles(image);
    const known = new Set([...sourceModules, ...sourceFiles].map((item) => lower(item.name)));
    for (const name of removals) check(known.has(name), `${name} is not in the image, so it cannot be removed`);

    const fixed = fixedSections(sourceModules);
    const fixedEnd = fixed.end;
    const fixedModules = fixed.modules;
    for (const name of fixedModules) check(!removals.has(lower(name)), `${name} runs in place and cannot be removed`);

    let modules = sourceModules.filter((module) => !removals.has(lower(module.name)));
    for (const { from, to } of rename) {
        const module = modules.find((candidate) => lower(candidate.name) === lower(from));
        check(module, `${from} is not a module, so it cannot be renamed`);
        check(!fixedModules.has(module.name), `${module.name} runs in place and cannot be renamed`);
        module.name = to;
    }
    const replaced = new Set(addedModules.map((module) => lower(module.name)));
    for (const module of modules) {
        if (replaced.has(lower(module.name))) {
            const addition = addedModules.find((candidate) => lower(candidate.name) === lower(module.name));
            if (addition.kernel === undefined) addition.kernel = module.virtualBase >= kernelSpaceStart;
            check(!fixedModules.has(module.name), `${module.name} runs in place and cannot be replaced`);
        }
    }
    modules = modules.filter((module) => !replaced.has(lower(module.name)));

    const ranges = addressRanges(modules);
    for (const addition of addedModules) modules.push(newModule(addition, ranges));
    const names = new Set();
    for (const module of modules) {
        check(!names.has(lower(module.name)), `Two modules are named ${module.name}`);
        names.add(lower(module.name));
    }

    let files = sourceFiles.filter((file) => !removals.has(lower(file.name)));
    const fileTemplate = files.find((file) => !file.file.compressed);
    check(fileTemplate, 'No uncompressed ROM file to take attributes from');
    const replaceFile = (name, contents) => {
        const existing = files.find((file) => lower(file.name) === lower(name));
        const entry = (existing ?? fileTemplate).entry.slice();
        writeU32(entry, 0, existing ? existing.file.attributes & ~0x800 : fileTemplate.file.attributes);
        writeU32(entry, 12, contents.length);
        writeU32(entry, 16, contents.length);
        const file = { name, entry, stored: contents, added: true };
        if (existing) files[files.indexOf(existing)] = file;
        else files.push(file);
    };
    for (const { name, contents } of addedFiles) {
        check(!names.has(lower(name)), `${name} is both a module and a file`);
        replaceFile(name, contents);
    }
    if (registryAdditions.length) {
        const registryFile = sourceFiles.find((file) => lower(file.name) === 'default.fdf');
        check(registryFile, 'The image has no default.fdf');
        const keys = readRegistry(readFileContents(image, registryFile.file));
        for (const text of registryAdditions) applyRegFile(keys, text);
        replaceFile('default.fdf', writeRegistry(keys));
    }

    const layout = new Layout(header.ulRAMStart - imageBase);
    layout.bytes.set(bytes.subarray(0, pageSize));
    for (const section of fixed.sections) {
        layout.bytes.set(bytes.subarray(section.sourceOffset, section.sourceOffset + storedLength(section)), section.sourceOffset);
    }
    layout.cursor = fixedEnd;
    layout.holes.push(...fixed.holes);

    for (const module of modules) {
        for (const section of module.sections) {
            section.storedLength = module.added ? section.physicalSize : storedLength(section);
            if (!section.physicalSize) continue;
            if (fixed.sections.has(section)) {
                section.newOffset = section.sourceOffset;
                continue;
            }
            if (section.flags & sectionWritable) continue;
            const data = module.added ? section.data : bytes.subarray(section.sourceOffset, section.sourceOffset + section.storedLength);
            section.newOffset = layout.placePages(data, section.virtualSize || section.physicalSize, `${module.name} section at rva ${hex(section.rva)}`);
        }
    }
    for (const module of modules) {
        for (const section of module.sections) {
            if (!section.physicalSize || section.newOffset !== undefined) continue;
            const data = module.added ? section.data : bytes.subarray(section.sourceOffset, section.sourceOffset + section.storedLength);
            section.newOffset = layout.placePacked(data, `${module.name} data at rva ${hex(section.rva)}`);
        }
    }
    for (const file of files) file.newOffset = layout.placePacked(file.stored, file.name);

    for (const patch of wordPatches) applyWordPatch(layout, modules, patch);

    const copyEntries = listCopyEntries(image).map((entry) => {
        const section = modules.flatMap((module) => module.sections).find((candidate) => candidate.dataPointer === entry.source && candidate.newOffset !== undefined);
        check(section, `Copy entry ${entry.index} source ${hex(entry.source)} is not a module section`);
        check(entry.copyLength <= section.storedLength, `Copy entry ${entry.index} is longer than its section`);
        return { ...entry, source: imageBase + section.newOffset };
    });

    for (const module of modules) {
        const o32 = new Uint8Array(module.sections.length * o32RecordSize);
        module.sections.forEach((section, index) => {
            const dataPointer = section.newOffset !== undefined ? imageBase + section.newOffset : section.dataPointer ?? 0;
            const base = index * o32RecordSize;
            writeU32(o32, base, section.virtualSize);
            writeU32(o32, base + 4, section.rva);
            writeU32(o32, base + 8, section.physicalSize);
            writeU32(o32, base + 12, dataPointer);
            writeU32(o32, base + 16, section.realAddress);
            writeU32(o32, base + 20, section.flags);
        });
        const e32 = new Uint8Array(e32RecordSize);
        e32.set(module.e32);
        module.e32Offset = layout.placePacked(e32, `${module.name} e32`);
        module.o32Offset = layout.placePacked(o32, `${module.name} o32`);
        module.nameOffset = layout.placePacked(encodeLatin1(`${module.name}\0`), `${module.name} name`);
        if (module.added) module.loadOffset = imageBase + (module.sections.find((section) => section.newOffset !== undefined)?.newOffset ?? module.e32Offset);
    }
    for (const file of files) file.nameOffset = layout.placePacked(encodeLatin1(`${file.name}\0`), `${file.name} name`);

    const copyTable = new Uint8Array(copyEntries.length * copyEntrySize);
    copyEntries.forEach((entry, index) => {
        writeU32(copyTable, index * copyEntrySize, entry.source);
        writeU32(copyTable, index * copyEntrySize + 4, entry.destination);
        writeU32(copyTable, index * copyEntrySize + 8, entry.copyLength);
        writeU32(copyTable, index * copyEntrySize + 12, entry.destinationLength);
    });
    const copyOffset = copyEntries.length ? layout.placePacked(copyTable, 'copy entries') : 0;

    const romHeaderOffset = align(layout.cursor, 4);
    const tocOffset = romHeaderOffset + romHeaderSize;
    const filesOffset = tocOffset + modules.length * tocEntrySize;
    const end = filesOffset + files.length * filesEntrySize;
    layout.grow(end, 'ROM header and tables');
    const out = layout.bytes;

    modules.forEach((module, index) => {
        const entry = tocOffset + index * tocEntrySize;
        writeU32(out, entry, module.attributes);
        out.set(module.time, entry + 4);
        writeU32(out, entry + 12, module.fileSize);
        writeU32(out, entry + 16, imageBase + module.nameOffset);
        writeU32(out, entry + 20, imageBase + module.e32Offset);
        writeU32(out, entry + 24, imageBase + module.o32Offset);
        writeU32(out, entry + 28, module.loadOffset);
    });
    files.forEach((file, index) => {
        const entry = filesOffset + index * filesEntrySize;
        out.set(file.entry, entry);
        writeU32(out, entry + 20, imageBase + file.nameOffset);
        writeU32(out, entry + 24, imageBase + file.newOffset);
    });

    const finalRanges = addressRanges(modules);
    const romHeader = bytes.slice(header.offset, header.offset + romHeaderSize);
    writeU32(romHeader, 0, ((finalRanges.user.bottom >>> 16) << 16 | finalRanges.kernel.bottom >>> 16) >>> 0);
    writeU32(romHeader, 4, ((finalRanges.user.top >>> 16) << 16 | finalRanges.kernel.top >>> 16) >>> 0);
    writeU32(romHeader, 12, imageBase + end);
    writeU32(romHeader, 16, modules.length);
    writeU32(romHeader, 32, copyEntries.length);
    writeU32(romHeader, 36, copyEntries.length ? imageBase + copyOffset : 0);
    writeU32(romHeader, 48, files.length);
    out.set(romHeader, romHeaderOffset);

    const tocPointers = [];
    for (const section of fixed.sections) {
        for (let offset = section.sourceOffset; offset + 4 <= section.sourceOffset + storedLength(section); offset += 4) {
            if (readU32(bytes, offset) === header.va) tocPointers.push(offset);
        }
    }
    check(tocPointers.length === 1, `Expected one pTOC in the kernel's fixed region, found ${tocPointers.length}`);
    check(readU32(bytes, headerSignatureOffset + 4) === header.va && readU32(bytes, headerSignatureOffset + 8) === header.offset, 'Image header does not point at the ROM header');
    writeU32(out, tocPointers[0], imageBase + romHeaderOffset);
    writeU32(out, headerSignatureOffset + 4, imageBase + romHeaderOffset);
    writeU32(out, headerSignatureOffset + 8, romHeaderOffset);

    if (!addedModules.length && !removals.size) notes.push('nothing added or removed: the image only moves Sharp\'s modules and files');
    return {
        bytes: out.slice(0, end),
        notes,
        modules: modules.map((module) => ({
            name: module.name, added: Boolean(module.added), virtualBase: module.virtualBase, virtualSize: module.virtualSize,
            linkedBase: module.linkedBase, relocationCount: module.relocationCount, kernel: module.kernel,
        })),
        files: files.map((file) => ({ name: file.name, added: Boolean(file.added), length: file.stored.length })),
        fixedEnd,
        romHeaderVa: imageBase + romHeaderOffset,
        dllFirst: readU32(romHeader, 0),
        dllLast: readU32(romHeader, 4),
    };
}

export function makeImageBundle({ sourceBytes, built, path }) {
    const firstPage = sourceBytes.subarray(0, pageSize);
    const headerRecords = makePatchRecords(0, firstPage, built.subarray(0, pageSize));
    check(headerRecords.every((record) => record.offset >= headerSignatureOffset && record.offset + record.expected.length <= headerSignatureOffset + 12),
        'The built image differs from the source outside the boot header in its first page');
    const skipEnd = Math.floor(sourceBytes.length / 512) * 512;
    check(built.length <= skipEnd, `The built image (${built.length} bytes) is larger than the image U-Boot loads (${skipEnd} bytes it can skip)`);
    const body = built.subarray(pageSize);
    const load = { offset: pageSize, length: body.length, crc32: crc32(body), path };
    const text = `skip ${pageSize.toString(16)} ${skipEnd.toString(16)} ${sourceBytes.length.toString(16)}\npatch image\n${formatPatch(sourceBytes.length, headerRecords, 0, [load])}`;
    return { text, body, load };
}

export function describeBuild(result) {
    const lines = [];
    for (const module of result.modules.filter((candidate) => candidate.added)) {
        lines.push(`${module.name}: ${module.kernel ? 'kernel' : 'user'} module at ${hex(module.virtualBase)}${module.relocationCount ? `, rebased from ${hex(module.linkedBase)} (${module.relocationCount} relocations)` : ''}`);
    }
    lines.push(`${result.modules.length} modules, ${result.files.length} files, ${result.bytes.length} bytes; dllfirst ${hex(result.dllFirst)}, dlllast ${hex(result.dllLast)}, ROM header ${hex(result.romHeaderVa)}`);
    return lines;
}
