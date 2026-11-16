import { check, toHex } from './bytes.mjs';

const maximumRecordBytes = 64;

export function makePatchRecords(imageOffset, original, replacement) {
    check(replacement.length === original.length, 'Replacement must be the same size as the original');
    const records = [];
    let index = 0;
    while (index < original.length) {
        if (original[index] === replacement[index]) {
            index++;
            continue;
        }
        const start = index;
        while (index < original.length && original[index] !== replacement[index] && index - start < maximumRecordBytes) index++;
        records.push({
            offset: imageOffset + start,
            expected: original.subarray(start, index),
            replacement: replacement.subarray(start, index),
        });
    }
    return records;
}

export function formatPatch(imageLength, records, extendedLength, loads = []) {
    const lines = [extendedLength ? `image ${imageLength.toString(16)} ${extendedLength.toString(16)}` : `image ${imageLength.toString(16)}`];
    for (const load of loads) {
        lines.push(`load ${load.offset.toString(16)} ${load.length.toString(16)} ${load.crc32.toString(16)} ${load.path}`);
    }
    for (const record of records) {
        lines.push(`${record.offset.toString(16)} ${toHex(record.expected)} ${toHex(record.replacement)}`);
    }
    return lines.join('\n') + '\n';
}
