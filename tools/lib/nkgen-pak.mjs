import { bytesEqual, check, crc32, encodeLatin1, latin1, readU32, toHex, writeU32 } from './bytes.mjs';
import { parseRegFile } from './registry-edit.mjs';

export const pakMagic = 'NKGENPAK';
export const pakFormat = 1;
export const pakHeaderSize = 72;
export const pakChecksumStart = 20;

export const operationKinds = {
    module_file: 1,
    module_rom: 2,
    registry: 3,
    word_patch: 4,
    a7300_display: 5,
    card_file: 6,
    card_file_rom: 7,
};
const operationNames = Object.fromEntries(Object.entries(operationKinds).map(([name, kind]) => [kind, name]));

export const registryEntryKinds = { key: 0, value: 1 };
export const registrySources = { literal: 0, device: 1, description: 2 };

const imageRecordSize = 48;
const profileRecordSize = 16;
const operationRecordSize = 16;
const blobRecordSize = 12;
const noOrdinal = 0xffffffff;
const align = (value, alignment) => Math.ceil(value / alignment) * alignment;

class PakWriter {
    constructor() {
        this.stringBytes = [];
        this.stringLength = 0;
        this.stringOffsets = new Map();
        this.blobs = [];
    }

    string(text) {
        check(typeof text === 'string', `Pak string is not a string: ${text}`);
        check([...text].every((character) => character.charCodeAt(0) >= 1 && character.charCodeAt(0) <= 0x7f), `Pak strings must be ASCII: ${text}`);
        if (!this.stringOffsets.has(text)) {
            this.stringOffsets.set(text, pakHeaderSize + this.stringLength);
            const bytes = encodeLatin1(`${text}\0`);
            this.stringBytes.push(bytes);
            this.stringLength += bytes.length;
        }
        return this.stringOffsets.get(text);
    }

    blob(bytes) {
        const checksum = crc32(bytes);
        const index = this.blobs.findIndex((blob) => blob.crc32 === checksum && bytesEqual(blob.bytes, bytes));
        if (index >= 0) return index;
        this.blobs.push({ bytes, crc32: checksum });
        return this.blobs.length - 1;
    }
}

const words = (values) => {
    const bytes = new Uint8Array(values.length * 4);
    values.forEach((value, index) => writeU32(bytes, index * 4, value >>> 0));
    return bytes;
};

const concatenate = (parts) => {
    const bytes = new Uint8Array(parts.reduce((total, part) => total + part.length, 0));
    parts.reduce((offset, part) => {
        bytes.set(part, offset);
        return offset + part.length;
    }, 0);
    return bytes;
};

const hexBytes = (text, label) => {
    check(typeof text === 'string' && /^([0-9a-fA-F]{2})+$/.test(text), `${label} is not a whole number of hex bytes: ${text}`);
    return Uint8Array.from(text.match(/../g), (pair) => Number.parseInt(pair, 16));
};

function encodeRomSpec(writer, rom) {
    const redirects = rom.redirectImports ?? [];
    const patches = rom.patchBytes ?? [];
    const imports = redirects.flatMap((redirect) => Object.entries(redirect.imports).map(([key, name]) => (/^\d+$/.test(key)
        ? [Number(key), 0, writer.string(name)]
        : [noOrdinal, writer.string(key), writer.string(name)])));
    const header = [writer.string(rom.module), rom.spareSections ?? 0, redirects.length, patches.length];
    let firstImport = 0;
    const redirectRecords = redirects.flatMap((redirect) => {
        const count = Object.keys(redirect.imports).length;
        const record = [writer.string(redirect.from), writer.string(redirect.to), firstImport, count];
        firstImport += count;
        return record;
    });
    const fixedLength = (header.length + redirectRecords.length + imports.length * 3 + patches.length * 5) * 4;
    const data = [];
    let dataOffset = fixedLength;
    const addData = (bytes) => {
        const offset = dataOffset;
        data.push(bytes);
        dataOffset += bytes.length;
        return offset;
    };
    const patchRecords = patches.flatMap((patch) => {
        const original = hexBytes(patch.original, `${rom.module} ${patch.offset} original`);
        const replacement = hexBytes(patch.replacement, `${rom.module} ${patch.offset} replacement`);
        check(/^[0-9a-fA-F]+$/.test(patch.offset), `${rom.module} patch offset is not hex: ${patch.offset}`);
        return [Number.parseInt(patch.offset, 16), original.length, addData(original), addData(replacement), writer.string(patch.purpose ?? '')];
    });
    const body = concatenate([words([...header, ...redirectRecords, ...imports.flat(), ...patchRecords]), ...data]);
    const padded = new Uint8Array(align(body.length, 4));
    padded.set(body);
    return padded;
}

function decodeRomSpec(pak, bytes) {
    const [module, spareSections, redirectCount, patchCount] = [0, 4, 8, 12].map((offset) => readU32(bytes, offset));
    const redirectStart = 16;
    const importStart = redirectStart + redirectCount * 16;
    const redirects = Array.from({ length: redirectCount }, (_, index) => {
        const base = redirectStart + index * 16;
        return { from: readU32(bytes, base), to: readU32(bytes, base + 4), first: readU32(bytes, base + 8), count: readU32(bytes, base + 12) };
    });
    const importCount = redirects.reduce((total, redirect) => total + redirect.count, 0);
    const patchStart = importStart + importCount * 12;
    return {
        module: pak.string(module),
        spareSections,
        redirectImports: redirects.map((redirect) => ({
            from: pak.string(redirect.from),
            to: pak.string(redirect.to),
            imports: Object.fromEntries(Array.from({ length: redirect.count }, (_, index) => {
                const base = importStart + (redirect.first + index) * 12;
                const ordinal = readU32(bytes, base);
                return [ordinal === noOrdinal ? pak.string(readU32(bytes, base + 4)) : String(ordinal), pak.string(readU32(bytes, base + 8))];
            })),
        })),
        patchBytes: Array.from({ length: patchCount }, (_, index) => {
            const base = patchStart + index * 20;
            const length = readU32(bytes, base + 4);
            const original = readU32(bytes, base + 8);
            const replacement = readU32(bytes, base + 12);
            check(original + length <= bytes.length && replacement + length <= bytes.length, 'ROM patch bytes outside their blob');
            return {
                offset: readU32(bytes, base).toString(16),
                original: toHex(bytes.subarray(original, original + length)),
                replacement: toHex(bytes.subarray(replacement, replacement + length)),
                purpose: pak.string(readU32(bytes, base + 16)),
            };
        }),
    };
}

function encodeRegistry(writer, entries) {
    const records = entries.flatMap((entry) => {
        if (entry.name === null) return [registryEntryKinds.key, entry.hive, writer.string(entry.keyPath), 0, 0, 0, 0];
        const source = entry.source ?? registrySources.literal;
        const value = entry.type === 4 ? entry.value : source === registrySources.literal ? writer.string(entry.value) : 0;
        check(entry.type === 1 || entry.type === 4, `Registry type ${entry.type} is not supported in a pak`);
        return [registryEntryKinds.value, entry.hive, writer.string(entry.keyPath), writer.string(entry.name), entry.type, value, source];
    });
    return words([entries.length, ...records]);
}

function decodeRegistry(pak, bytes, match) {
    return Array.from({ length: readU32(bytes, 0) }, (_, index) => {
        const [kind, hive, keyPath, name, type, value, source] = Array.from({ length: 7 }, (_, field) => readU32(bytes, 4 + index * 28 + field * 4));
        if (kind === registryEntryKinds.key) return { hive, keyPath: pak.string(keyPath), name: null };
        const sourced = { [registrySources.device]: match.device, [registrySources.description]: match.image.description };
        return { hive, keyPath: pak.string(keyPath), name: pak.string(name), type, value: type === 4 ? value : sourced[source] ?? pak.string(value) };
    });
}

const encodeWordPatch = (writer, patch) => words([
    writer.string(patch.module), patch.words.length,
    ...patch.words.flatMap((word) => [word.address, word.original, word.replacement, writer.string(word.purpose ?? '')]),
]);

const decodeWordPatch = (pak, bytes) => ({
    module: pak.string(readU32(bytes, 0)),
    words: Array.from({ length: readU32(bytes, 4) }, (_, index) => ({
        address: readU32(bytes, 8 + index * 16),
        original: readU32(bytes, 12 + index * 16),
        replacement: readU32(bytes, 16 + index * 16),
        purpose: pak.string(readU32(bytes, 20 + index * 16)),
    })),
});

function encodeSkipList(writer, skipList) {
    const tested = skipList.tested_images.map((sha256) => hexBytes(sha256, 'tested image SHA-256'));
    return concatenate([
        words([skipList.minimum_span, skipList.modules.length, skipList.files.length, tested.length,
            ...skipList.modules.map((name) => writer.string(name)), ...skipList.files.map((name) => writer.string(name))]),
        ...tested,
    ]);
}

function decodeSkipList(pak, bytes) {
    const [minimumSpan, moduleCount, fileCount, testedCount] = [0, 4, 8, 12].map((offset) => readU32(bytes, offset));
    const names = (start, count) => Array.from({ length: count }, (_, index) => pak.string(readU32(bytes, start + index * 4)));
    const testedStart = 16 + (moduleCount + fileCount) * 4;
    return {
        minimum_span: minimumSpan,
        modules: names(16, moduleCount),
        files: names(16 + moduleCount * 4, fileCount),
        tested_images: Array.from({ length: testedCount }, (_, index) => toHex(bytes.subarray(testedStart + index * 32, testedStart + index * 32 + 32))),
    };
}

export const modelRegistryEntries = (profile, releases) => [
    { hive: 2, keyPath: 'Software\\Brain', name: null },
    { hive: 2, keyPath: 'Software\\Brain', name: 'Model', type: 1, source: registrySources.device },
    { hive: 2, keyPath: 'Software\\Brain', name: 'Profile', type: 1, value: profile.id },
    { hive: 2, keyPath: 'Software\\Brain', name: 'Image', type: 1, source: registrySources.description },
    ...(releases.length ? [
        { hive: 2, keyPath: 'Software\\Brain\\Modules', name: null },
        ...releases.map(([name, release]) => ({ hive: 2, keyPath: 'Software\\Brain\\Modules', name, type: 1, value: release })),
    ] : []),
];

export async function makePak({ definition, skipList, bstart, release, load, releasesOf, parseWordPatch }) {
    const writer = new PakWriter();
    const releaseString = writer.string(release);
    const images = [];
    const profiles = [];
    const operations = [];
    const decoder = new TextDecoder();
    const add = (kind, name, bytes) => operations.push([operationKinds[kind], writer.string(name), writer.blob(bytes), 0]);

    for (const [profileIndex, profile] of definition.profiles.entries()) {
        for (const image of profile.images) {
            check(/^[0-9a-f]{64}$/.test(image.sha256), `${profile.id} image SHA-256 is not 64 lowercase hex digits`);
            images.push({ sha256: hexBytes(image.sha256, 'image SHA-256'), fields: [image.length, profileIndex, writer.string(image.device ?? profile.device), writer.string(image.description)] });
        }
        const firstOperation = operations.length;
        for (const entry of profile.modules ?? []) {
            if (entry.rom) add('module_rom', entry.name, encodeRomSpec(writer, entry.rom));
            else add('module_file', entry.name, await load(entry.file));
        }
        const registryTexts = [];
        for (const path of profile.registry ?? []) registryTexts.push(decoder.decode(await load(path)));
        const registryEntries = [...(registryTexts.length ? parseRegFile(registryTexts.join('\n')) : []), ...modelRegistryEntries(profile, releasesOf(profile))];
        add('registry', 'registry', encodeRegistry(writer, registryEntries));
        for (const path of profile.wordPatches ?? []) {
            const patch = parseWordPatch(decoder.decode(await load(path)));
            add('word_patch', patch.module, encodeWordPatch(writer, patch));
        }
        if (profile.a7300Display) add('a7300_display', 'a7300-display', await load(profile.a7300Display));
        for (const entry of profile.cardFiles ?? []) {
            if (entry.rom) add('card_file_rom', entry.path, encodeRomSpec(writer, entry.rom));
            else add('card_file', entry.path, await load(entry.file));
        }
        profiles.push([writer.string(profile.id), firstOperation, operations.length - firstOperation, 0]);
    }
    const imageHashes = new Set(definition.profiles.flatMap((profile) => profile.images.map((image) => image.sha256)));
    for (const sha256 of skipList.tested_images) check(imageHashes.has(sha256), `Skip list tested image ${sha256} has no profile; the generator only hashes images a profile lists`);

    const bstartBlob = writer.blob(bstart);
    const skipListBlob = writer.blob(encodeSkipList(writer, skipList));

    const strings = concatenate(writer.stringBytes);
    const stringsEnd = align(pakHeaderSize + strings.length, 4);
    const imagesOffset = stringsEnd;
    const profilesOffset = imagesOffset + images.length * imageRecordSize;
    const operationsOffset = profilesOffset + profiles.length * profileRecordSize;
    const blobsOffset = operationsOffset + operations.length * operationRecordSize;
    let dataOffset = blobsOffset + writer.blobs.length * blobRecordSize;
    const blobPlacements = writer.blobs.map((blob) => {
        const offset = dataOffset;
        dataOffset = align(dataOffset + blob.bytes.length, 4);
        return offset;
    });

    const pak = new Uint8Array(dataOffset);
    pak.set(encodeLatin1(pakMagic), 0);
    pak.set(words([pakFormat, pak.length, 0, releaseString, pakHeaderSize, strings.length, imagesOffset, images.length,
        profilesOffset, profiles.length, operationsOffset, operations.length, blobsOffset, writer.blobs.length, bstartBlob, skipListBlob]), 8);
    pak.set(strings, pakHeaderSize);
    images.forEach((image, index) => {
        pak.set(image.sha256, imagesOffset + index * imageRecordSize);
        pak.set(words(image.fields), imagesOffset + index * imageRecordSize + 32);
    });
    pak.set(words(profiles.flat()), profilesOffset);
    pak.set(words(operations.flat()), operationsOffset);
    pak.set(words(writer.blobs.flatMap((blob, index) => [blobPlacements[index], blob.bytes.length, blob.crc32])), blobsOffset);
    writer.blobs.forEach((blob, index) => pak.set(blob.bytes, blobPlacements[index]));
    writeU32(pak, 16, crc32(pak.subarray(pakChecksumStart)));
    return pak;
}

export function readPak(bytes) {
    check(bytes.length >= pakHeaderSize && latin1(bytes, 0, 8) === pakMagic, 'Not a generator pak (no NKGENPAK magic)');
    const field = (index) => readU32(bytes, 8 + index * 4);
    check(field(0) === pakFormat, `Pak format ${field(0)} is not ${pakFormat}`);
    check(field(1) === bytes.length, `Pak length ${field(1)} differs from the file's ${bytes.length}`);
    check(field(2) === crc32(bytes.subarray(pakChecksumStart)), 'Pak CRC-32 does not match');
    const [stringsOffset, stringsLength] = [field(4), field(5)];
    const string = (offset) => {
        check(offset >= stringsOffset && offset < stringsOffset + stringsLength, `Pak string offset ${offset} is outside the string table`);
        const end = bytes.indexOf(0, offset);
        check(end >= 0 && end < stringsOffset + stringsLength, `Pak string at ${offset} is unterminated`);
        return latin1(bytes, offset, end);
    };
    const blobs = Array.from({ length: field(13) }, (_, index) => {
        const base = field(12) + index * blobRecordSize;
        const [offset, length, checksum] = [0, 4, 8].map((delta) => readU32(bytes, base + delta));
        check(offset + length <= bytes.length, `Pak blob ${index} is outside the pak`);
        const blob = bytes.subarray(offset, offset + length);
        check(crc32(blob) === checksum, `Pak blob ${index} CRC-32 does not match`);
        return { offset, bytes: blob, crc32: checksum };
    });
    const blob = (index) => {
        check(index < blobs.length, `Pak blob ${index} does not exist`);
        return blobs[index].bytes;
    };
    const pak = { string, blob };
    const operations = Array.from({ length: field(11) }, (_, index) => {
        const base = field(10) + index * operationRecordSize;
        const kind = readU32(bytes, base);
        check(operationNames[kind], `Pak operation ${index} has unknown kind ${kind}`);
        return { kind: operationNames[kind], name: string(readU32(bytes, base + 4)), blob: readU32(bytes, base + 8), argument: readU32(bytes, base + 12) };
    });
    const profiles = Array.from({ length: field(9) }, (_, index) => {
        const base = field(8) + index * profileRecordSize;
        const [first, count] = [readU32(bytes, base + 4), readU32(bytes, base + 8)];
        check(first + count <= operations.length, `Pak profile ${index} operations are out of range`);
        return { id: string(readU32(bytes, base)), operations: operations.slice(first, first + count), flags: readU32(bytes, base + 12) };
    });
    const images = Array.from({ length: field(7) }, (_, index) => {
        const base = field(6) + index * imageRecordSize;
        const profile = readU32(bytes, base + 36);
        check(profile < profiles.length, `Pak image ${index} names profile ${profile}`);
        return {
            sha256: toHex(bytes.subarray(base, base + 32)), length: readU32(bytes, base + 32), profile,
            device: string(readU32(bytes, base + 40)), description: string(readU32(bytes, base + 44)),
        };
    });
    return {
        ...pak,
        length: bytes.length,
        crc32: field(2),
        release: string(field(3)),
        images,
        profiles,
        blobs,
        bstart: blob(field(14)),
        skipList: decodeSkipList(pak, blob(field(15))),
    };
}

export function findPakProfile(pak, imageSha256) {
    const image = pak.images.find((candidate) => candidate.sha256 === imageSha256);
    if (!image) return null;
    const profile = pak.profiles[image.profile];
    return { profile, image, device: image.device };
}

export function planFromPak(pak, match) {
    const plan = { modules: [], registryEntries: [], wordPatches: [], a7300Display: null, cardFiles: [] };
    for (const operation of match.profile.operations) {
        const bytes = pak.blob(operation.blob);
        if (operation.kind === 'module_file') plan.modules.push({ name: operation.name, contents: bytes });
        else if (operation.kind === 'module_rom') plan.modules.push({ name: operation.name, rom: decodeRomSpec(pak, bytes) });
        else if (operation.kind === 'registry') plan.registryEntries.push(...decodeRegistry(pak, bytes, match));
        else if (operation.kind === 'word_patch') plan.wordPatches.push(decodeWordPatch(pak, bytes));
        else if (operation.kind === 'a7300_display') plan.a7300Display = bytes;
        else if (operation.kind === 'card_file') plan.cardFiles.push({ path: operation.name, contents: bytes });
        else if (operation.kind === 'card_file_rom') plan.cardFiles.push({ path: operation.name, rom: decodeRomSpec(pak, bytes) });
    }
    return plan;
}

export function describePak(pak) {
    const lines = [`release ${pak.release}`, `length ${pak.length}, crc32 ${pak.crc32.toString(16).padStart(8, '0')}, ${pak.blobs.length} blobs`];
    lines.push(`bstart ${pak.bstart.length} bytes`);
    lines.push(`skip list: minimum span ${pak.skipList.minimum_span}, ${pak.skipList.modules.length} modules, ${pak.skipList.files.length} files`);
    for (const sha256 of pak.skipList.tested_images) lines.push(`  tested ${sha256}`);
    for (const image of pak.images) lines.push(`image ${image.sha256} ${image.length} ${pak.profiles[image.profile].id} ${image.device}: ${image.description}`);
    for (const profile of pak.profiles) {
        lines.push(`profile ${profile.id}`);
        for (const operation of profile.operations) {
            const bytes = pak.blob(operation.blob);
            lines.push(`  ${operation.kind} ${operation.name}: blob ${operation.blob}, ${bytes.length} bytes, crc32 ${crc32(bytes).toString(16).padStart(8, '0')}`);
            if (operation.kind === 'registry') {
                const match = { device: '<device>', image: { description: '<description>' } };
                for (const entry of decodeRegistry(pak, bytes, match)) {
                    lines.push(entry.name === null ? `    [${entry.hive} ${entry.keyPath}]` : `    "${entry.name}"=${entry.type === 4 ? `dword:${entry.value.toString(16).padStart(8, '0')}` : `"${entry.value}"`}`);
                }
            } else if (operation.kind === 'word_patch') {
                for (const word of decodeWordPatch(pak, bytes).words) lines.push(`    ${word.address.toString(16)} ${word.original.toString(16)} ${word.replacement.toString(16)} ${word.purpose}`);
            } else if (operation.kind === 'module_rom' || operation.kind === 'card_file_rom') {
                lines.push(`    ${JSON.stringify(decodeRomSpec(pak, bytes))}`);
            }
        }
    }
    return lines.join('\n') + '\n';
}
