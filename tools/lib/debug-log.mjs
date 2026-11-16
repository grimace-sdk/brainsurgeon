import { check, hex, readU32, writeU32 } from './bytes.mjs';
import { makePatchRecords } from './nk-patch.mjs';
import { imageBase, listModules } from './rom-image.mjs';

const hookEntry = 0xe92d4001;
const hookOriginalOffset = 20;
const hookRingOffset = 24;
const ringBytes = 0x10000;
const ramPhysical = 0x40000000;
const cachedRam = 0x80000000;
const uncachedRam = 0xa0000000;
const imagePhysical = ramPhysical + imageBase - cachedRam;
export const defaultRingAddress = 0x40180000;
const oemGlobalDataOffset = 0xf0;
const oemGlobalVersion = 0x60000;
const oemGlobalWriteDebugString = 0x10;

export function oemGlobalWriteDebugStringOffset(image, bytes) {
    const kernel = listModules(image).find((module) => module.name.toLowerCase() === 'nk.exe');
    check(kernel, 'nk.exe not found');
    const data = kernel.sections[2];
    check(data && !data.compressed && data.dataOffset !== null, 'nk.exe initialised data not found');
    const oemGlobal = data.dataOffset + oemGlobalDataOffset;
    check(readU32(bytes, oemGlobal) === oemGlobalVersion, `OEMGLOBAL version at ${hex(oemGlobal)} is ${hex(readU32(bytes, oemGlobal))}`);
    const offset = oemGlobal + oemGlobalWriteDebugString;
    const original = readU32(bytes, offset);
    const text = kernel.sections[0];
    check(original >= text.dataPointer && original < text.dataPointer + text.virtualSize, `OEMGLOBAL pfnWriteDebugString ${hex(original)} is outside nk.exe code`);
    return { offset, original };
}

export function debugLogCode(hook, original, ringAddress = defaultRingAddress) {
    check(hook.length > hookRingOffset + 4 && readU32(hook, 0) === hookEntry, 'Not a debug-log hook (unexpected entry)');
    check(readU32(hook, hookOriginalOffset) === 0 && readU32(hook, hookRingOffset) === 0, 'Debug-log hook already holds its addresses');
    check(ringAddress % ringBytes === 0 && ringAddress >= ramPhysical && ringAddress + ringBytes <= imagePhysical,
        `Ring ${hex(ringAddress)} must be 64 KB aligned RAM below the image`);
    const code = hook.slice();
    writeU32(code, hookOriginalOffset, original);
    writeU32(code, hookRingOffset, uncachedRam + ringAddress - ramPhysical);
    return code;
}

export function debugLogRecords(bytes, pointer, hookAddress) {
    check(hookAddress >= imageBase, `Hook address ${hex(hookAddress)} is below the image`);
    const replacement = new Uint8Array(4);
    writeU32(replacement, 0, hookAddress);
    return makePatchRecords(pointer.offset, bytes.subarray(pointer.offset, pointer.offset + 4), replacement);
}
