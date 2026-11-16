import { check, readU16, readU32, toHex, utf16le } from './bytes.mjs';

export const flatRegistrySignature = 0x1d8374b2;
export const hiveNames = { 0: 'HKEY_CLASSES_ROOT', 1: 'HKEY_CURRENT_USER', 2: 'HKEY_LOCAL_MACHINE' };

export function parseFlatRegistry(bytes) {
    check(readU32(bytes, 0) === flatRegistrySignature, 'Unexpected flat-registry signature');
    check(readU32(bytes, 4) === bytes.length, 'Flat-registry length mismatch');
    const keys = [];
    let current;
    let offset = 8;
    let records = 0;
    const string = (start, characters, end) => {
        check(characters > 0 && start + characters * 2 <= end, 'Registry string outside record');
        const value = utf16le(bytes, start, start + characters * 2);
        check(value.endsWith('\0'), 'Registry string lacks terminator');
        return value.slice(0, -1);
    };
    while (offset < bytes.length) {
        check(offset + 4 <= bytes.length, 'Truncated registry record');
        const length = readU16(bytes, offset);
        const kind = readU16(bytes, offset + 2);
        const start = offset + 4;
        const end = start + length;
        check(end <= bytes.length, 'Registry record exceeds input');
        if (kind === 1) {
            check(length >= 4, 'Registry key record too short');
            const characters = readU16(bytes, start + 2);
            check(length === 4 + characters * 2, 'Registry key record length mismatch');
            current = { hive: readU16(bytes, start), key: string(start + 4, characters, end), values: [] };
            Object.defineProperty(current, 'recordOffset', { value: offset });
            keys.push(current);
        } else if (kind === 2) {
            check(current && length >= 6, 'Registry value has no key');
            const type = readU16(bytes, start);
            const characters = readU16(bytes, start + 2);
            const size = readU16(bytes, start + 4);
            check(length === 6 + characters * 2 + size, 'Registry value record length mismatch');
            const name = characters ? string(start + 6, characters, end) : '';
            const dataStart = start + 6 + characters * 2;
            const data = bytes.subarray(dataStart, end);
            let value;
            if (type === 4) {
                check(size === 4, 'Registry DWORD is not 4 bytes');
                value = readU32(data, 0);
            } else if (type === 1 || type === 2 || type === 7) {
                check(size % 2 === 0, 'Registry string has an odd length');
                value = utf16le(data).replace(/\0+$/, '');
                if (type === 7) value = value.split('\0');
            } else {
                value = toHex(data);
            }
            const entry = { name, type, value };
            Object.defineProperties(entry, {
                recordOffset: { value: offset },
                dataOffset: { value: dataStart },
                dataSize: { value: size },
            });
            current.values.push(entry);
        } else {
            throw new Error(`Unsupported registry record kind ${kind} at 0x${offset.toString(16)}`);
        }
        records++;
        offset = end;
    }
    check(offset === bytes.length, 'Registry records do not end at the file end');
    return { bytes: bytes.length, records, keys };
}
