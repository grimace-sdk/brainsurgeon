import { readFileSync } from 'node:fs';
import { encodeUtf16le, toHex } from '../lib/bytes.mjs';
import { hiveNames, parseFlatRegistry } from '../lib/flat-registry.mjs';
import { findFile, hex, openImage, readFileContents, slackAfter } from '../lib/rom-image.mjs';

const usage = 'Usage: node tools/nk/dump-registry.mjs IMAGE [KEY_PATTERN]';
const [imagePath, patternText] = process.argv.slice(2);
if (!imagePath) {
    console.error(usage);
    process.exit(1);
}
const pattern = patternText === undefined ? null : new RegExp(patternText, 'i');

const image = openImage(new Uint8Array(readFileSync(imagePath)));
const file = findFile(image, 'default.fdf');
const bytes = readFileContents(image, file);
const registry = parseFlatRegistry(bytes);
const imageOffset = (fdfOffset) => file.compressed ? '-' : hex(file.loadOffset + fdfOffset);

const quote = (text) => `"${text.replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"`;
const hexBytes = (hexText) => hexText.match(/../g)?.join(',') ?? '';
const formatValue = ({ type, value }) => {
    if (type === 1) return quote(value);
    if (type === 4) return `dword:${value.toString(16).padStart(8, '0')}`;
    if (type === 2) return `hex(2):${hexBytes(toHex(encodeUtf16le(`${value}\0`)))} ; ${quote(value)}`;
    if (type === 7) return `hex(7):${value.map(quote).join(',')}`;
    if (type === 3) return `hex:${hexBytes(value)}`;
    return `hex(${type}):${hexBytes(value)}`;
};

const selected = registry.keys.filter((key) => !pattern || pattern.test(`${hiveNames[key.hive] ?? key.hive}\\${key.key}`));
const slack = slackAfter(image, file.loadOffset + file.compressedSize);
console.log(`; default.fdf: ${file.realSize} bytes at image offset ${hex(file.loadOffset)} (VA ${hex(file.loadVa)}), ${file.compressed ? 'compressed' : 'stored'}`);
console.log(`; ${registry.records} records, ${registry.keys.length} keys, ${selected.length} selected`);
console.log('; flat registry has no internal free space: the header length equals the file length and records are packed end to end');
console.log(`; image slack after default.fdf: ${slack.bytes} bytes to ${hex(slack.nextOffset)} (${slack.nextName}), ${slack.allZero ? 'all zero' : 'not all zero'}`);
for (const key of selected) {
    console.log(`\n[${hiveNames[key.hive] ?? key.hive}\\${key.key}] ; fdf ${hex(key.recordOffset)} image ${imageOffset(key.recordOffset)}`);
    for (const value of key.values) {
        const name = value.name === '' ? '@' : quote(value.name);
        console.log(`${name}=${formatValue(value)} ; data image ${imageOffset(value.dataOffset)} size ${value.dataSize}`);
    }
}
