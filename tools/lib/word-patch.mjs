import { check, hex, readU32, writeU32 } from './bytes.mjs';
import { makePatchRecords } from './nk-patch.mjs';
import { listModules } from './rom-image.mjs';

const parseWord = (value, label) => {
    const number = typeof value === 'number' ? value : Number.parseInt(String(value), 16);
    check(Number.isInteger(number) && number >= 0 && number <= 0xffffffff, `${label} is not a 32-bit value: ${value}`);
    return number >>> 0;
};

export function parseWordPatch(text) {
    const patch = JSON.parse(text);
    check(typeof patch.module === 'string' && Array.isArray(patch.words) && patch.words.length, 'A word patch needs "module" and a non-empty "words" list');
    return {
        module: patch.module,
        words: patch.words.map((word, index) => ({
            address: parseWord(word.address, `words[${index}].address`),
            original: parseWord(word.original, `words[${index}].original`),
            replacement: parseWord(word.replacement, `words[${index}].replacement`),
            purpose: word.purpose ?? '',
        })),
    };
}

export function wordPatchRecords(image, bytes, patch) {
    const module = listModules(image).find((candidate) => candidate.name.toLowerCase() === patch.module.toLowerCase());
    check(module, `${patch.module} not found`);
    return patch.words.flatMap(({ address, original, replacement, purpose }) => {
        check(address % 4 === 0, `${hex(address)} is not word aligned`);
        const section = module.sections.find((candidate) => candidate.dataOffset !== null &&
            address >= candidate.realAddress && address + 4 <= candidate.realAddress + candidate.virtualSize);
        check(section, `${hex(address)} (${purpose}) is outside ${patch.module}'s sections`);
        check(!section.compressed, `${hex(address)} (${purpose}) is in a compressed section`);
        const offset = section.dataOffset + address - section.realAddress;
        check(readU32(bytes, offset) === original, `${hex(address)} (${purpose}) is ${hex(readU32(bytes, offset))}, expected ${hex(original)}`);
        const replacementBytes = new Uint8Array(4);
        writeU32(replacementBytes, 0, replacement);
        return makePatchRecords(offset, bytes.subarray(offset, offset + 4), replacementBytes);
    });
}
