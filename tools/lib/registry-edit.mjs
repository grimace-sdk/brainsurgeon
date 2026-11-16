import { check, encodeUtf16le, writeU32 } from './bytes.mjs';
import { flatRegistrySignature, hiveNames, parseFlatRegistry } from './flat-registry.mjs';

const hiveNumbers = Object.fromEntries(Object.entries(hiveNames).map(([number, name]) => [name, Number(number)]));
hiveNumbers.HKLM = 2;
hiveNumbers.HKCU = 1;
hiveNumbers.HKCR = 0;

const writeU16 = (bytes, offset, value) => {
    bytes[offset] = value & 0xff;
    bytes[offset + 1] = (value >> 8) & 0xff;
};

const record = (kind, body) => {
    const bytes = new Uint8Array(4 + body.length);
    writeU16(bytes, 0, body.length);
    writeU16(bytes, 2, kind);
    bytes.set(body, 4);
    return bytes;
};

const valueData = ({ type, value, raw }) => {
    if (raw) return raw;
    if (type === 4) {
        const data = new Uint8Array(4);
        writeU32(data, 0, value >>> 0);
        return data;
    }
    if (type === 1 || type === 2) return encodeUtf16le(`${value}\0`);
    if (type === 7) return encodeUtf16le(`${value.join('\0')}\0\0`);
    throw new Error(`Cannot encode registry type ${type}`);
};

export function readRegistry(bytes) {
    const parsed = parseFlatRegistry(bytes);
    return parsed.keys.map((key) => ({
        hive: key.hive,
        key: key.key,
        values: key.values.map((value) => ({
            name: value.name,
            type: value.type,
            value: value.value,
            raw: bytes.slice(value.dataOffset, value.dataOffset + value.dataSize),
        })),
    }));
}

const padToWord = (keys) => {
    const total = 8 + keys.reduce((sum, key) => sum + 4 + 4 + (key.key.length + 1) * 2
        + key.values.reduce((valueSum, value) => valueSum + 4 + 6 + (value.name ? (value.name.length + 1) * 2 : 0) + valueData(value).length, 0), 0);
    if (total % 4 === 0) return;
    const padded = [...keys].reverse().flatMap((key) => [...key.values].reverse()).find((value) => (value.type === 1 || value.type === 2) && !value.raw);
    check(padded, 'No string value to pad the registry to a 4-byte length');
    padded.raw = new Uint8Array([...valueData(padded), 0, 0]);
};

export function writeRegistry(keys) {
    padToWord(keys);
    const records = [];
    for (const key of keys) {
        const name = encodeUtf16le(`${key.key}\0`);
        const body = new Uint8Array(4 + name.length);
        writeU16(body, 0, key.hive);
        writeU16(body, 2, name.length / 2);
        body.set(name, 4);
        records.push(record(1, body));
        for (const value of key.values) {
            const valueName = value.name ? encodeUtf16le(`${value.name}\0`) : new Uint8Array(0);
            const data = valueData(value);
            check(data.length <= 0xffff && valueName.length + data.length + 6 <= 0xffff, `${key.key}\\${value.name} is too large`);
            const valueBody = new Uint8Array(6 + valueName.length + data.length);
            writeU16(valueBody, 0, value.type);
            writeU16(valueBody, 2, valueName.length / 2);
            writeU16(valueBody, 4, data.length);
            valueBody.set(valueName, 6);
            valueBody.set(data, 6 + valueName.length);
            records.push(record(2, valueBody));
        }
    }
    const length = 8 + records.reduce((total, item) => total + item.length, 0);
    const bytes = new Uint8Array(length);
    writeU32(bytes, 0, flatRegistrySignature);
    writeU32(bytes, 4, length);
    let offset = 8;
    for (const item of records) {
        bytes.set(item, offset);
        offset += item.length;
    }
    return bytes;
}

export function setRegistryValue(keys, hive, keyPath, name, type, value) {
    let key = keys.find((candidate) => candidate.hive === hive && candidate.key.toLowerCase() === keyPath.toLowerCase());
    if (!key) {
        key = { hive, key: keyPath, values: [] };
        keys.push(key);
    }
    const existing = key.values.find((candidate) => candidate.name.toLowerCase() === name.toLowerCase());
    if (existing) Object.assign(existing, { type, value, raw: null });
    else key.values.push({ name, type, value, raw: null });
}

const unquote = (text) => text.slice(1, -1).replace(/\\(.)/g, '$1');

export function parseRegFile(text) {
    const entries = [];
    let hive = null;
    let keyPath = null;
    for (const [index, rawLine] of text.split(/\r?\n/).entries()) {
        const line = rawLine.replace(/^﻿/, '').trim();
        if (!line || line.startsWith(';') || /^REGEDIT4$|^Windows Registry Editor/.test(line)) continue;
        const keyMatch = line.match(/^\[([^\\\]]+)\\(.+)\]$/);
        if (keyMatch) {
            hive = hiveNumbers[keyMatch[1].toUpperCase()];
            check(hive !== undefined, `line ${index + 1}: unknown hive ${keyMatch[1]}`);
            keyPath = keyMatch[2];
            entries.push({ hive, keyPath, name: null });
            continue;
        }
        check(keyPath !== null, `line ${index + 1}: value before any key`);
        const valueMatch = line.match(/^(@|"(?:[^"\\]|\\.)*")=(.+)$/);
        check(valueMatch, `line ${index + 1}: cannot parse ${line}`);
        const name = valueMatch[1] === '@' ? '' : unquote(valueMatch[1]);
        const data = valueMatch[2];
        let type;
        let value;
        if (/^"(?:[^"\\]|\\.)*"$/.test(data)) {
            type = 1;
            value = unquote(data);
        } else if (/^dword:[0-9a-fA-F]{1,8}$/.test(data)) {
            type = 4;
            value = Number.parseInt(data.slice(6), 16);
        } else {
            throw new Error(`line ${index + 1}: only strings and dwords are supported: ${data}`);
        }
        entries.push({ hive, keyPath, name, type, value });
    }
    return entries;
}

export function applyRegFile(keys, text) {
    for (const entry of parseRegFile(text)) {
        if (entry.name === null) {
            if (!keys.some((key) => key.hive === entry.hive && key.key.toLowerCase() === entry.keyPath.toLowerCase())) {
                keys.push({ hive: entry.hive, key: entry.keyPath, values: [] });
            }
            continue;
        }
        setRegistryValue(keys, entry.hive, entry.keyPath, entry.name, entry.type, entry.value);
    }
    return keys;
}
