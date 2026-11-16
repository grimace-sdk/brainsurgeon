import { crc32, encodeLatin1, writeU32 } from './bytes.mjs';

const dosDate = (1 << 5) | 1;

const writeU16 = (bytes, offset, value) => {
    bytes[offset] = value & 0xff;
    bytes[offset + 1] = (value >> 8) & 0xff;
};

export function makeZip(files) {
    const locals = [];
    const centrals = [];
    let offset = 0;
    for (const file of files) {
        const name = encodeLatin1(file.path);
        const checksum = crc32(file.contents);
        const local = new Uint8Array(30 + name.length);
        writeU32(local, 0, 0x04034b50);
        writeU16(local, 4, 10);
        writeU16(local, 12, dosDate);
        writeU32(local, 14, checksum);
        writeU32(local, 18, file.contents.length);
        writeU32(local, 22, file.contents.length);
        writeU16(local, 26, name.length);
        local.set(name, 30);
        const central = new Uint8Array(46 + name.length);
        writeU32(central, 0, 0x02014b50);
        writeU16(central, 4, 20);
        writeU16(central, 6, 10);
        writeU16(central, 14, dosDate);
        writeU32(central, 16, checksum);
        writeU32(central, 20, file.contents.length);
        writeU32(central, 24, file.contents.length);
        writeU16(central, 28, name.length);
        writeU32(central, 42, offset);
        central.set(name, 46);
        locals.push(local, file.contents);
        centrals.push(central);
        offset += local.length + file.contents.length;
    }
    const centralSize = centrals.reduce((total, central) => total + central.length, 0);
    const end = new Uint8Array(22);
    writeU32(end, 0, 0x06054b50);
    writeU16(end, 8, files.length);
    writeU16(end, 10, files.length);
    writeU32(end, 12, centralSize);
    writeU32(end, 16, offset);
    const zip = new Uint8Array(offset + centralSize + end.length);
    [...locals, ...centrals, end].reduce((position, part) => {
        zip.set(part, position);
        return position + part.length;
    }, 0);
    return zip;
}
