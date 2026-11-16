import { check, encodeUtf16le, hex, readU32, writeU32 } from './bytes.mjs';
import { makePatchRecords } from './nk-patch.mjs';
import { listModules } from './rom-image.mjs';

export const a7300DisplayImageSha256s = [
    '894a227cd6a262b157cb1a8dc6c9dbfa521f757ab90a7fa9e18ab60cefaa3d81',
    '0844883d47f5b0d7d53b58f67619459049bfb213c27c71f143414f79a882a4ca',
];

const driverName = 'ddraw_mx28.dll';
const driverTextAddress = 0xc06f1000;
const panelWrite = 0xc06f72e4;
const frameTransfer = 0xc06f70fc;
const trampolineAddress = 0xc06f1dc0;
const trampolineString = 'C:\\WINCE600\\';
const movePointer = 0xc06fa770;
const movePointerEntryOffset = 4;
const panelUpdateSlot = 0xc06f1944;
const panelUpdate = 0xc06f63a4;
const panelUpdateEntryOffset = 8;
const writeDebugStringEntryOffset = 12;
const refreshEntryOffset = 16;
const refreshCallSite = 0xc06f82b0;
const noOperation = 0xe1a00000;
const oemGlobalDataOffset = 0xf0;
const oemGlobalVersion = 0x60000;
const oemGlobalWriteDebugString = 0x10;
const kernelWriteDebugString = 0x8020c700;
const loadPcFromNextWord = 0xe51ff004;

const instructionPatches = [
    { address: 0xc06f6328, original: 0xe3a01028, replacement: 0xe3a01008, purpose: 'MADCTL data 28 -> 08: no row/column exchange' },
    { address: 0xc06f6390, original: 0xe3a01034, replacement: 0xe3a01035, purpose: 'tearing effect off (34) -> on (35)' },
    { address: 0xc06f49c0, original: 0xe59f0060, replacement: 0xea00000b, purpose: 'TE watchdog: skip the panel re-initialisation, which the hook TE wait can trigger falsely' },
];

const branch = (from, to, link) => ((link ? 0xeb000000 : 0xea000000) | (((to - (from + 8)) >> 2) & 0x00ffffff)) >>> 0;

export function a7300DisplayRecords(image, bytes, hookAddress) {
    const driver = listModules(image).find((module) => module.name.toLowerCase() === driverName.toLowerCase());
    check(driver, `${driverName} not found`);
    const text = driver.sections[0];
    check(text.realAddress === driverTextAddress && !text.compressed, `${driverName} code is not at ${hex(driverTextAddress)} or is compressed`);
    const offsetOf = (address) => {
        check(address >= text.realAddress && address + 4 <= text.realAddress + text.virtualSize, `${hex(address)} is outside ${driverName} code`);
        return text.dataOffset + address - text.realAddress;
    };

    const tearOnTail = trampolineAddress;
    const hookTrampoline = trampolineAddress + 12;
    const refreshTrampoline = trampolineAddress + 20;
    const words = [
        ...instructionPatches,
        { address: 0xc06f63a0, original: 0xe12fff1e, replacement: branch(0xc06f63a0, tearOnTail, false), purpose: 'end of panel init: send the 35 parameter' },
        { address: 0xc06f655c, original: branch(0xc06f655c, frameTransfer, true), replacement: branch(0xc06f655c, hookTrampoline, true), purpose: 'frame transfer through the portrait hook' },
        { address: movePointer, original: 0xe92d41f0, replacement: loadPcFromNextWord, purpose: 'MovePointer: jump to the hook, which also marks the cursor areas dirty' },
        { address: movePointer + 4, original: 0xe1a07002, replacement: hookAddress + movePointerEntryOffset, purpose: 'MovePointer hook address' },
        { address: panelUpdateSlot, original: panelUpdate, replacement: hookAddress + panelUpdateEntryOffset, purpose: 'panel controller update (vtable slot 6): send only the dirty rectangle' },
        { address: refreshCallSite, original: 0xe28d1000, replacement: 0xe1a00006, purpose: 'refresh thread: pass the driver to the hook' },
        { address: refreshCallSite + 4, original: 0xe1a00006, replacement: branch(refreshCallSite + 4, refreshTrampoline, true), purpose: 'refresh thread: send merged dirty tiles instead of one bounding rectangle' },
        { address: refreshCallSite + 8, original: 0xebfffefd, replacement: noOperation, purpose: 'refresh thread: original collect call' },
        { address: refreshCallSite + 12, original: 0xe28d1000, replacement: noOperation, purpose: 'refresh thread: original argument' },
        { address: refreshCallSite + 16, original: 0xe1a00006, replacement: noOperation, purpose: 'refresh thread: original argument' },
        { address: refreshCallSite + 20, original: 0xebffff85, replacement: noOperation, purpose: 'refresh thread: original send call' },
    ];
    const records = words.flatMap(({ address, original, replacement, purpose }) => {
        const offset = offsetOf(address);
        check(readU32(bytes, offset) === original, `${hex(address)} (${purpose}) is ${hex(readU32(bytes, offset))}, expected ${hex(original)}`);
        const replacementBytes = new Uint8Array(4);
        writeU32(replacementBytes, 0, replacement);
        return makePatchRecords(offset, bytes.subarray(offset, offset + 4), replacementBytes);
    });

    const oal = listModules(image).find((module) => module.name.toLowerCase() === 'nk.exe');
    check(oal, 'nk.exe not found');
    const oalData = oal.sections[2];
    check(oalData && !oalData.compressed && oalData.dataOffset !== null, 'nk.exe initialised data not found');
    const oemGlobal = oalData.dataOffset + oemGlobalDataOffset;
    check(readU32(bytes, oemGlobal) === oemGlobalVersion, `OEMGLOBAL version at ${hex(oemGlobal)} is ${hex(readU32(bytes, oemGlobal))}`);
    check(readU32(bytes, oemGlobal + oemGlobalWriteDebugString) === kernelWriteDebugString, 'OEMGLOBAL pfnWriteDebugString is not the expected OEMWriteDebugString');
    const writeDebugString = new Uint8Array(4);
    writeU32(writeDebugString, 0, hookAddress + writeDebugStringEntryOffset);
    records.push(...makePatchRecords(oemGlobal + oemGlobalWriteDebugString, bytes.subarray(oemGlobal + oemGlobalWriteDebugString, oemGlobal + oemGlobalWriteDebugString + 4), writeDebugString));

    const trampolineOffset = offsetOf(trampolineAddress);
    const expectedString = encodeUtf16le(trampolineString);
    check(bytes.subarray(trampolineOffset, trampolineOffset + expectedString.length).every((value, index) => value === expectedString[index]),
        `${hex(trampolineAddress)} does not hold the expected debug string`);
    const trampolines = new Uint8Array(28);
    writeU32(trampolines, 0, 0xe3a00001);
    writeU32(trampolines, 4, 0xe3a01000);
    writeU32(trampolines, 8, branch(tearOnTail + 8, panelWrite, false));
    writeU32(trampolines, 12, 0xe51ff004);
    writeU32(trampolines, 16, hookAddress);
    writeU32(trampolines, 20, 0xe51ff004);
    writeU32(trampolines, 24, hookAddress + refreshEntryOffset);
    records.push(...makePatchRecords(trampolineOffset, bytes.subarray(trampolineOffset, trampolineOffset + trampolines.length), trampolines));
    return records;
}
