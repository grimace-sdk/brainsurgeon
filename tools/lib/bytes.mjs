export function check(condition, message) {
    if (!condition) throw new Error(message);
}

export const hex = (value, width = 8) => `0x${value.toString(16).padStart(width, '0')}`;

export const readU16 = (bytes, offset) => {
    check(offset >= 0 && offset + 2 <= bytes.length, `Read of 2 bytes at ${offset} outside ${bytes.length}`);
    return bytes[offset] | bytes[offset + 1] << 8;
};

export const readU32 = (bytes, offset) => {
    check(offset >= 0 && offset + 4 <= bytes.length, `Read of 4 bytes at ${offset} outside ${bytes.length}`);
    return (bytes[offset] | bytes[offset + 1] << 8 | bytes[offset + 2] << 16 | bytes[offset + 3] << 24) >>> 0;
};

export const readU24 = (bytes, offset) => readU16(bytes, offset) | bytes[offset + 2] << 16;

export const readI32 = (bytes, offset) => readU32(bytes, offset) | 0;

export const writeU32 = (bytes, offset, value) => {
    bytes[offset] = value & 0xff;
    bytes[offset + 1] = value >>> 8 & 0xff;
    bytes[offset + 2] = value >>> 16 & 0xff;
    bytes[offset + 3] = value >>> 24 & 0xff;
};

export const latin1 = (bytes, start = 0, end = bytes.length) => {
    let text = '';
    for (let index = start; index < end; index++) text += String.fromCharCode(bytes[index]);
    return text;
};

export const utf16le = (bytes, start = 0, end = bytes.length) => new TextDecoder('utf-16le').decode(bytes.subarray(start, end));

export const encodeLatin1 = (text) => Uint8Array.from(text, (character) => character.charCodeAt(0) & 0xff);

export const encodeUtf16le = (text) => {
    const bytes = new Uint8Array(text.length * 2);
    for (let index = 0; index < text.length; index++) {
        const code = text.charCodeAt(index);
        bytes[index * 2] = code & 0xff;
        bytes[index * 2 + 1] = code >>> 8;
    }
    return bytes;
};

export const toHex = (bytes) => Array.from(bytes, (byte) => byte.toString(16).padStart(2, '0')).join('');

export const bytesEqual = (first, second) => first.length === second.length && first.every((byte, index) => byte === second[index]);

export const indexOfByte = (bytes, value, start) => {
    for (let index = start; index < bytes.length; index++) if (bytes[index] === value) return index;
    return -1;
};

export async function sha256Hex(bytes) {
    return toHex(new Uint8Array(await globalThis.crypto.subtle.digest('SHA-256', bytes)));
}

const crc32Table = Array.from({ length: 256 }, (_, index) => {
    let value = index;
    for (let bit = 0; bit < 8; bit++) value = value & 1 ? 0xedb88320 ^ (value >>> 1) : value >>> 1;
    return value >>> 0;
});

export function crc32(bytes) {
    let value = 0xffffffff;
    for (const byte of bytes) value = crc32Table[(value ^ byte) & 0xff] ^ (value >>> 8);
    return (value ^ 0xffffffff) >>> 0;
}
