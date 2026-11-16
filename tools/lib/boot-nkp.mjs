import { check, crc32, encodeLatin1, encodeUtf16le, readU32, sha256Hex, writeU32 } from './bytes.mjs';
import { parseFlatRegistry } from './flat-registry.mjs';
import { applyRegFile, readRegistry, setRegistryValue, writeRegistry } from './registry-edit.mjs';
import { a7300DisplayImageSha256s, a7300DisplayRecords } from './a7300-display.mjs';
import { debugLogCode, debugLogRecords, oemGlobalWriteDebugStringOffset } from './debug-log.mjs';
import { wordPatchRecords } from './word-patch.mjs';
import { formatPatch, makePatchRecords } from './nk-patch.mjs';
import { findFile, imageBase, imageFromDumpOrSlot, listFiles, listModules, listUsedRegions, openImage, readFileContents } from './rom-image.mjs';

const romHeaderNumberOfFiles = 48;
const romHeaderPhysicalLast = 12;
const filesEntrySize = 28;
const sectorSize = 512;
const codeAlignment = 32;
const align = (value, alignment) => Math.ceil(value / alignment) * alignment;
export async function makeBootNkp({ contents, slot, bstart, skipList, skip = 'auto', a7300Display = null, debugLog = null, debugLogAddress, moduleReplacements = [], registryAdditions = null, wordPatches = [] }) {
    const source = imageFromDumpOrSlot(contents, slot);
    const bytes = source.bytes;
    const image = openImage(bytes);
    const imageSha256 = await sha256Hex(bytes);
    const notes = [];
    const skipListTested = skipList.tested_images.includes(imageSha256);
    const skipping = skip === 'auto' ? skipListTested : Boolean(skip);
    if (skip === 'auto' && !skipListTested) notes.push('skip list has not been checked against this image, so nothing is skipped; turn skipping on for a slightly faster boot');

    const registryStringRecords = (hive, keyPath, valueName, newText) => {
        const file = findFile(image, 'default.fdf');
        check(!file.compressed, 'default.fdf is compressed in this image; the registry cannot be patched in place');
        const registry = parseFlatRegistry(readFileContents(image, file));
        const key = registry.keys.find((candidate) => candidate.hive === hive && candidate.key.toLowerCase() === keyPath.toLowerCase());
        check(key, `Registry key ${keyPath} not found`);
        const value = key.values.find((candidate) => candidate.name.toLowerCase() === valueName.toLowerCase());
        check(value, `Registry value ${keyPath}\\${valueName} not found`);
        const encoded = encodeUtf16le(`${newText}\0`);
        check(encoded.length <= value.dataSize, `${newText} does not fit in ${value.dataSize} bytes`);
        const offset = file.loadOffset + value.dataOffset;
        const original = bytes.subarray(offset, offset + value.dataSize);
        const replacement = new Uint8Array(value.dataSize);
        replacement.set(encoded);
        return makePatchRecords(offset, original, replacement);
    };

    const spans = skipSpans();
    const spanRoom = spans.map((span) => ({ ...span }));
    const carveSpan = (length, alignment) => {
        const span = spanRoom.reduce((largest, candidate) => (!largest || candidate.end - candidate.start > largest.end - largest.start ? candidate : largest), null);
        const offset = span ? Math.floor((span.end - length) / alignment) * alignment : -1;
        check(span && offset >= span.start, `${length} bytes do not fit before ulRAMStart ${image.header.ulRAMStart.toString(16)} or in a skipped span; turn skipping on`);
        span.end = offset;
        return offset;
    };

    const appendFileRecords = (name, fileContents, previous = null, reservedEntries = 0) => {
        const header = image.header;
        const fileCount = previous ? previous.fileCount : header.numfiles;
        const physicalLast = previous ? previous.physicalLast : header.physlast;
        const uncompressed = listFiles(image).filter((file) => !file.compressed);
        const template = uncompressed.find((file) => file.name.toLowerCase() === 'eddesktop.bmp') ?? uncompressed[0];
        check(template, 'No uncompressed ROM file to copy attributes from');
        const headerOffset = image.offsetOf(image.romHeaderVa, 84);
        const entryOffset = physicalLast - imageBase;
        const nameOffset = entryOffset + filesEntrySize * (1 + reservedEntries);
        const nameBytes = encodeLatin1(`${name}\0`);
        const nameEnd = align(nameOffset + nameBytes.length, 4);
        const fitsAtEnd = align(nameEnd + fileContents.length, 4) + imageBase <= header.ulRAMStart;
        const dataOffset = fitsAtEnd ? nameEnd : carveSpan(fileContents.length, 4);
        const newEnd = fitsAtEnd ? align(dataOffset + fileContents.length, 4) : nameEnd;
        check(newEnd + imageBase <= header.ulRAMStart, `${name} would end at ${(newEnd + imageBase).toString(16)}, past ulRAMStart ${header.ulRAMStart.toString(16)}`);
        const entry = bytes.slice(template.entryOffset, template.entryOffset + filesEntrySize);
        writeU32(entry, 0, template.attributes);
        writeU32(entry, 12, fileContents.length);
        writeU32(entry, 16, fileContents.length);
        writeU32(entry, 20, nameOffset + imageBase);
        writeU32(entry, 24, dataOffset + imageBase);
        const appended = new Uint8Array(newEnd - entryOffset);
        appended.set(entry, 0);
        appended.set(nameBytes, nameOffset - entryOffset);
        if (fitsAtEnd) appended.set(fileContents, dataOffset - entryOffset);
        const headerOriginal = previous ? previous.header : bytes.subarray(headerOffset, headerOffset + 84);
        const headerReplacement = headerOriginal.slice();
        writeU32(headerReplacement, romHeaderNumberOfFiles, fileCount + 1);
        writeU32(headerReplacement, romHeaderPhysicalLast, newEnd + imageBase);
        return {
            extendedLength: newEnd,
            dataAddress: dataOffset + imageBase,
            fileCount: fileCount + 1,
            physicalLast: newEnd + imageBase,
            header: headerReplacement,
            reservedEntryOffsets: Array.from({ length: reservedEntries }, (_, index) => entryOffset + filesEntrySize * (1 + index)),
            records: [
                ...makePatchRecords(headerOffset, headerOriginal, headerReplacement),
                ...makePatchRecords(entryOffset, new Uint8Array(appended.length), appended),
                ...(fitsAtEnd ? [] : makePatchRecords(dataOffset, new Uint8Array(fileContents.length), fileContents)),
            ],
        };
    };

    const replacements = [...moduleReplacements];
    if (registryAdditions) {
        const keys = readRegistry(readFileContents(image, findFile(image, 'default.fdf')));
        setRegistryValue(keys, 2, 'init', 'Launch50', 1, 'bstart.exe');
        applyRegFile(keys, registryAdditions);
        replacements.push({ name: 'default.fdf', contents: writeRegistry(keys) });
    }

    const romEntry = (name) => {
        const module = listModules(image).find((candidate) => candidate.name.toLowerCase() === name.toLowerCase());
        if (module) return { name: module.name, nameOffset: image.offsetOf(readU32(bytes, module.tocOffset + 16)) };
        const file = listFiles(image).find((candidate) => candidate.name.toLowerCase() === name.toLowerCase());
        if (file) return { name: file.name, file };
        return null;
    };

    const addedEntries = replacements.filter((replacement) => !romEntry(replacement.name)?.file).length;
    const bstartAppended = appendFileRecords('bstart.exe', bstart, null, addedEntries);
    let entryIndex = 0;
    const bstartPatch = () => {
        const records = [...bstartAppended.records, ...registryStringRecords(2, 'init', 'Launch50', 'bstart.exe')];
        return formatPatch(bytes.length, records, bstartAppended.extendedLength);
    };

    const appendCodeRecords = (code, previous) => {
        const headerOffset = image.offsetOf(image.romHeaderVa, 84);
        const endOffset = align(previous.physicalLast - imageBase, codeAlignment);
        if (align(endOffset + code.length, 4) + imageBase > image.header.ulRAMStart) {
            const dataOffset = carveSpan(code.length, codeAlignment);
            return {
                extendedLength: previous.extendedLength,
                dataAddress: dataOffset + imageBase,
                header: previous.header,
                records: makePatchRecords(dataOffset, new Uint8Array(code.length), code),
            };
        }
        const dataOffset = endOffset;
        const newEnd = align(dataOffset + code.length, 4);
        check(newEnd + imageBase <= image.header.ulRAMStart, `Code would end at ${(newEnd + imageBase).toString(16)}, past ulRAMStart ${image.header.ulRAMStart.toString(16)}`);
        const headerReplacement = previous.header.slice();
        writeU32(headerReplacement, romHeaderPhysicalLast, newEnd + imageBase);
        const appended = new Uint8Array(newEnd - dataOffset);
        appended.set(code);
        return {
            extendedLength: newEnd,
            dataAddress: dataOffset + imageBase,
            header: headerReplacement,
            records: [
                ...makePatchRecords(headerOffset, previous.header, headerReplacement),
                ...makePatchRecords(dataOffset, new Uint8Array(appended.length), appended),
            ],
        };
    };

    const a7300DisplayPatch = () => {
        check(a7300DisplayImageSha256s.includes(imageSha256), 'The A7300 display patch is only for the PW-A7300 and PW-G5300 main images');
        const appended = appendCodeRecords(a7300Display, bstartAppended);
        const records = [...appended.records, ...a7300DisplayRecords(image, bytes, appended.dataAddress)];
        return { text: formatPatch(bytes.length, records, appended.extendedLength), header: appended.header, extendedLength: appended.extendedLength };
    };

    const debugLogPatch = (previous) => {
        check(!a7300Display, 'The A7300 display patch already installs the debug log hook');
        const pointer = oemGlobalWriteDebugStringOffset(image, bytes);
        const appended = appendCodeRecords(debugLogCode(debugLog, pointer.original, debugLogAddress), previous);
        const records = [...appended.records, ...debugLogRecords(bytes, pointer, appended.dataAddress)];
        return { text: formatPatch(bytes.length, records, appended.extendedLength), header: appended.header, extendedLength: appended.extendedLength };
    };

    const fileRepointPatch = (existing, replacement, previous, span) => {
        const original = bytes.subarray(existing.file.entryOffset, existing.file.entryOffset + filesEntrySize);
        const entry = original.slice();
        writeU32(entry, 12, replacement.contents.length);
        writeU32(entry, 16, replacement.contents.length);
        writeU32(entry, 24, span.start + imageBase);
        const load = { offset: span.start, length: replacement.contents.length, crc32: crc32(replacement.contents), path: `modules/${existing.name}` };
        return { text: formatPatch(bytes.length, makePatchRecords(existing.file.entryOffset, original, entry), previous.extendedLength, [load]), header: previous.header, span, renamed: null, repointed: true, load };
    };

    const moduleReplacementPatch = (replacement, previous, spans, usedSpans) => {
        const existing = romEntry(replacement.name);
        const span = spans.find((candidate) => !usedSpans.has(candidate) && candidate.end - candidate.start >= replacement.contents.length);
        check(span, `No skipped span holds ${replacement.name} (${replacement.contents.length} bytes); replacing a module needs skipping on`);
        usedSpans.add(span);
        if (existing?.file) return fileRepointPatch(existing, replacement, previous, span);
        const index = entryIndex++;
        const fileName = existing ? existing.name : replacement.name;
        const extension = fileName.lastIndexOf('.');
        check(extension > 0, `${fileName} has no extension`);
        const renamed = existing ? `${fileName.slice(0, extension - 1)}_${fileName.slice(extension)}` : null;
        const headerOffset = image.offsetOf(image.romHeaderVa, 84);
        const headerReplacement = previous.header.slice();
        writeU32(headerReplacement, romHeaderNumberOfFiles, previous.fileCount + 1 + index);
        const template = listFiles(image).find((file) => file.name.toLowerCase() === 'eddesktop.bmp') ?? listFiles(image)[0];
        const nameBytes = encodeLatin1(`${fileName}\0`);
        const nameOffset = span.start + align(replacement.contents.length, 4);
        check(nameOffset + nameBytes.length <= span.end, `${replacement.name} and its name do not fit in the skipped span`);
        const entry = bytes.slice(template.entryOffset, template.entryOffset + filesEntrySize);
        writeU32(entry, 0, template.attributes);
        writeU32(entry, 12, replacement.contents.length);
        writeU32(entry, 16, replacement.contents.length);
        writeU32(entry, 20, nameOffset + imageBase);
        writeU32(entry, 24, span.start + imageBase);
        const records = [
            ...makePatchRecords(headerOffset, previous.header, headerReplacement),
            ...makePatchRecords(bstartAppended.reservedEntryOffsets[index], new Uint8Array(filesEntrySize), entry),
            ...makePatchRecords(nameOffset, new Uint8Array(nameBytes.length), nameBytes),
            ...(existing ? makePatchRecords(existing.nameOffset, encodeLatin1(existing.name), encodeLatin1(renamed)) : []),
        ];
        const load = { offset: span.start, length: replacement.contents.length, crc32: crc32(replacement.contents), path: `modules/${fileName}` };
        return { text: formatPatch(bytes.length, records, previous.extendedLength, [load]), header: headerReplacement, span, renamed, load };
    };

    function skipSpans() {
        if (!skipping) return [];
        const modules = listModules(image);
        const files = listFiles(image);
        const present = (list, name) => list.some((item) => item.name.toLowerCase() === name.toLowerCase());
        const missing = [
            ...skipList.modules.filter((name) => !present(modules, name)),
            ...skipList.files.filter((name) => !present(files, name)),
        ];
        if (missing.length) notes.push(`skip list entries not in this image: ${missing.join(', ')}`);
        if (!skipListTested) notes.push('skip list was checked against a different image; turn skipping off if Windows CE misbehaves');
        const skipModules = new Set(skipList.modules.map((name) => name.toLowerCase()));
        const skipFiles = new Set(skipList.files.map((name) => name.toLowerCase()));
        const sectionData = (module) => module.sections
            .filter((section) => section.dataOffset !== null)
            .map((section) => ({ start: section.dataOffset, end: section.dataOffset + Math.min(section.physicalSize, section.virtualSize || section.physicalSize) }));
        const ranges = [
            ...modules.filter((module) => skipModules.has(module.name.toLowerCase())).flatMap(sectionData),
            ...files.filter((file) => skipFiles.has(file.name.toLowerCase())).map((file) => ({ start: file.loadOffset, end: file.loadOffset + file.compressedSize })),
        ]
            .map((range) => ({ start: Math.ceil(range.start / sectorSize) * sectorSize, end: Math.floor(range.end / sectorSize) * sectorSize }))
            .filter((range) => range.end > range.start)
            .sort((first, second) => first.start - second.start);
        const merged = [];
        for (const range of ranges) {
            const last = merged.at(-1);
            if (last && range.start <= last.end) last.end = Math.max(last.end, range.end);
            else merged.push({ ...range });
        }
        const spans = merged.filter((range) => range.end - range.start >= skipList.minimum_span);
        const kept = [
            ...listUsedRegions(image).filter((region) => !/ section \d+$/.test(region.name) && !skipFiles.has(region.name.toLowerCase())),
            ...modules.filter((module) => !skipModules.has(module.name.toLowerCase())).flatMap((module) => sectionData(module).map((range) => ({ ...range, name: module.name }))),
        ];
        for (const span of spans) {
            const overlap = kept.find((region) => region.end > span.start && region.start < span.end && region.end > region.start);
            check(!overlap, `Skip span ${span.start.toString(16)}-${span.end.toString(16)} overlaps ${overlap && overlap.name}`);
        }
        return spans;
    }

    const sections = [{ name: 'bstart', text: bstartPatch() }];
    let lastAppended = bstartAppended;
    if (a7300Display) {
        const display = a7300DisplayPatch();
        sections.push({ name: 'a7300-display', text: display.text });
        lastAppended = { ...bstartAppended, header: display.header, extendedLength: display.extendedLength };
    }
    if (debugLog) {
        const log = debugLogPatch(lastAppended);
        sections.push({ name: 'debug-log', text: log.text });
        lastAppended = { ...lastAppended, header: log.header, extendedLength: log.extendedLength };
    }
    const usedSpans = new Set();
    const replacedModules = [];
    replacements.forEach((replacement) => {
        const patch = moduleReplacementPatch(replacement, lastAppended, spanRoom, usedSpans);
        sections.push({ name: `module ${replacement.name}`, text: patch.text });
        lastAppended = { ...lastAppended, header: patch.header };
        replacedModules.push({ name: replacement.name, renamed: patch.renamed, address: patch.span.start + imageBase, length: replacement.contents.length, cardPath: patch.load.path, contents: replacement.contents, repointed: Boolean(patch.repointed) });
    });
    for (const patch of wordPatches) {
        check(!replacements.some((replacement) => replacement.name.toLowerCase() === patch.module.toLowerCase()), `${patch.module} is both replaced and word-patched`);
        sections.push({ name: `words ${patch.module}`, text: formatPatch(bytes.length, wordPatchRecords(image, bytes, patch), lastAppended.extendedLength) });
    }
    const bundle = [
        ...spans.map((span) => `skip ${span.start.toString(16)} ${span.end.toString(16)} ${bytes.length.toString(16)}`),
        ...sections.map((section) => `patch ${section.name}\n${section.text}`),
    ].join('\n').replace(/\n+$/, '') + '\n';

    return {
        bundle,
        slot: source.slot,
        skipListTested,
        skipping,
        imageLength: bytes.length,
        imageSha256,
        patches: sections.map((section) => section.name),
        replacedModules,
        spans,
        skippedBytes: spans.reduce((total, span) => total + span.end - span.start, 0),
        notes,
    };
}
