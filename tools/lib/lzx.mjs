import { check, readI32, readU24, readU32, writeU32 } from './bytes.mjs';

const romBlockSize = 4096;
const numChars = 256;
const minMatch = 2;
const numPrimaryLengths = 7;
const numSecondaryLengths = 249;
const blockTypeVerbatim = 1;
const blockTypeAligned = 2;
const blockTypeUncompressed = 3;
const positionSlotsByWindowBits = { 15: 30, 16: 32, 17: 34, 18: 36, 19: 38, 20: 42, 21: 50 };

const extraBits = [];
const positionBase = [];
for (let slot = 0, base = 0; slot < 50; slot++) {
    const bits = slot < 4 ? 0 : Math.min(Math.floor(slot / 2) - 1, 17);
    extraBits.push(bits);
    positionBase.push(base);
    base += 2 ** bits;
}

class BitReader {
    constructor(bytes) {
        this.bytes = bytes;
        this.position = 0;
        this.buffer = 0;
        this.bitsLeft = 0;
    }

    nextByte() {
        const value = this.position < this.bytes.length ? this.bytes[this.position] : 0;
        this.position++;
        return value;
    }

    ensure(count) {
        while (this.bitsLeft < count) {
            const low = this.nextByte();
            const high = this.nextByte();
            this.buffer = this.buffer * 65536 + (high << 8 | low);
            this.bitsLeft += 16;
        }
    }

    read(count) {
        if (count === 0) return 0;
        this.ensure(count);
        this.bitsLeft -= count;
        const divisor = 2 ** this.bitsLeft;
        const value = Math.floor(this.buffer / divisor);
        this.buffer -= value * divisor;
        return value;
    }

    alignToWord() {
        this.buffer = 0;
        this.bitsLeft = 0;
    }

    rawByte() {
        return this.nextByte();
    }
}

function buildHuffman(lengths) {
    const maxLength = 16;
    const counts = new Array(maxLength + 1).fill(0);
    for (const length of lengths) counts[length]++;
    counts[0] = 0;
    const offsets = new Array(maxLength + 2).fill(0);
    for (let length = 1; length <= maxLength; length++) offsets[length + 1] = offsets[length] + counts[length];
    const sortedSymbols = [];
    for (let length = 1; length <= maxLength; length++) {
        lengths.forEach((symbolLength, symbol) => {
            if (symbolLength === length) sortedSymbols.push(symbol);
        });
    }
    let kraftSum = 0;
    for (let length = 1; length <= maxLength; length++) kraftSum += counts[length] * 2 ** (maxLength - length);
    const complete = kraftSum === 2 ** maxLength;
    const empty = sortedSymbols.length === 0;
    return { counts, offsets, sortedSymbols, complete, empty };
}

function decodeSymbol(reader, table, name) {
    check(table.complete, `${name} Huffman table is not complete`);
    let code = 0;
    let first = 0;
    for (let length = 1; length <= 16; length++) {
        code = code * 2 + reader.read(1);
        const count = table.counts[length];
        if (code - first < count) return table.sortedSymbols[table.offsets[length] + code - first];
        first = (first + count) * 2;
    }
    throw new Error(`${name} Huffman code not found`);
}

function readLengths(reader, lengths, first, last) {
    const pretreeLengths = Array.from({ length: 20 }, () => reader.read(4));
    const pretree = buildHuffman(pretreeLengths);
    let index = first;
    while (index < last) {
        const symbol = decodeSymbol(reader, pretree, 'pretree');
        if (symbol === 17) {
            let run = reader.read(4) + 4;
            while (run--) lengths[index++] = 0;
        } else if (symbol === 18) {
            let run = reader.read(5) + 20;
            while (run--) lengths[index++] = 0;
        } else if (symbol === 19) {
            let run = reader.read(1) + 4;
            const delta = decodeSymbol(reader, pretree, 'pretree');
            let value = lengths[index] - delta;
            if (value < 0) value += 17;
            while (run--) lengths[index++] = value;
        } else {
            let value = lengths[index] - symbol;
            if (value < 0) value += 17;
            lengths[index++] = value;
        }
    }
}

function decompressLzxFrame(compressed, windowBits, frameSize) {
    const positionSlots = positionSlotsByWindowBits[windowBits];
    check(positionSlots, `Unsupported LZX window bits ${windowBits}`);
    check(frameSize <= 32768, 'Frame larger than 32 KiB');
    const mainTreeSymbols = numChars + positionSlots * 8;
    const reader = new BitReader(compressed);
    const window = new Uint8Array(frameSize);
    const mainLengths = new Array(mainTreeSymbols).fill(0);
    const lengthLengths = new Array(numSecondaryLengths).fill(0);
    let recentOffsets = [1, 1, 1];
    let windowPosition = 0;
    let blockType = 0;
    let blockLength = 0;
    let blockRemaining = 0;
    let mainTree;
    let lengthTree;
    let alignedTree;
    let intelStarted = false;

    let intelFileSize = 0;
    if (reader.read(1)) {
        const high = reader.read(16);
        const low = reader.read(16);
        intelFileSize = high * 65536 + low;
    }

    let bytesToDo = frameSize;
    while (bytesToDo > 0) {
        if (blockRemaining === 0) {
            if (blockType === blockTypeUncompressed && (blockLength & 1)) reader.rawByte();
            blockType = reader.read(3);
            const high = reader.read(16);
            const low = reader.read(8);
            blockLength = high * 256 + low;
            blockRemaining = blockLength;
            if (blockType === blockTypeAligned || blockType === blockTypeVerbatim) {
                if (blockType === blockTypeAligned) {
                    alignedTree = buildHuffman(Array.from({ length: 8 }, () => reader.read(3)));
                }
                readLengths(reader, mainLengths, 0, 256);
                readLengths(reader, mainLengths, 256, mainTreeSymbols);
                mainTree = buildHuffman(mainLengths);
                if (mainLengths[0xe8] !== 0) intelStarted = true;
                readLengths(reader, lengthLengths, 0, numSecondaryLengths);
                lengthTree = buildHuffman(lengthLengths);
                check(lengthTree.empty || lengthTree.complete, 'Length Huffman table is not complete');
            } else if (blockType === blockTypeUncompressed) {
                intelStarted = true;
                if (reader.bitsLeft === 0) reader.ensure(16);
                reader.alignToWord();
                const stored = Uint8Array.from({ length: 12 }, () => reader.rawByte());
                recentOffsets = [readU32(stored, 0), readU32(stored, 4), readU32(stored, 8)];
            } else {
                throw new Error(`Bad LZX block type ${blockType}`);
            }
        }

        let thisRun = Math.min(blockRemaining, bytesToDo);
        bytesToDo -= thisRun;
        blockRemaining -= thisRun;

        if (blockType === blockTypeUncompressed) {
            while (thisRun > 0) {
                window[windowPosition++] = reader.rawByte();
                thisRun--;
            }
        } else {
            while (thisRun > 0) {
                let mainElement = decodeSymbol(reader, mainTree, 'main');
                if (mainElement < numChars) {
                    window[windowPosition++] = mainElement;
                    thisRun--;
                    continue;
                }
                mainElement -= numChars;
                let matchLength = mainElement & numPrimaryLengths;
                if (matchLength === numPrimaryLengths) {
                    check(!lengthTree.empty, 'Length symbol needed but tree is empty');
                    matchLength += decodeSymbol(reader, lengthTree, 'length');
                }
                matchLength += minMatch;
                const slot = mainElement >> 3;
                let matchOffset;
                if (slot === 0) {
                    matchOffset = recentOffsets[0];
                } else if (slot === 1) {
                    matchOffset = recentOffsets[1];
                    recentOffsets = [matchOffset, recentOffsets[0], recentOffsets[2]];
                } else if (slot === 2) {
                    matchOffset = recentOffsets[2];
                    recentOffsets = [matchOffset, recentOffsets[1], recentOffsets[0]];
                } else {
                    const extra = slot >= 36 ? 17 : extraBits[slot];
                    matchOffset = positionBase[slot] - 2;
                    if (extra >= 3 && blockType === blockTypeAligned) {
                        if (extra > 3) matchOffset += reader.read(extra - 3) * 8;
                        matchOffset += decodeSymbol(reader, alignedTree, 'aligned');
                    } else if (extra) {
                        matchOffset += reader.read(extra);
                    }
                    recentOffsets = [matchOffset, recentOffsets[0], recentOffsets[1]];
                }
                check(matchOffset <= windowPosition, 'Match offset before start of stream');
                check(windowPosition + matchLength <= frameSize, 'Match runs past end of frame');
                for (let index = 0; index < matchLength; index++) {
                    window[windowPosition] = window[windowPosition - matchOffset];
                    windowPosition++;
                }
                thisRun -= matchLength;
            }
        }

        if (thisRun < 0) {
            check(-thisRun <= blockRemaining, 'Match overran LZX block');
            blockRemaining += thisRun;
        }
    }
    check(windowPosition === frameSize, 'Decoded beyond frame');

    if (intelStarted && intelFileSize && frameSize > 10) {
        let currentPosition = 0;
        let index = 0;
        while (index < frameSize - 10) {
            if (window[index++] !== 0xe8) {
                currentPosition++;
                continue;
            }
            const absoluteOffset = readI32(window, index);
            if (absoluteOffset >= -currentPosition && absoluteOffset < intelFileSize) {
                const relativeOffset = absoluteOffset >= 0 ? absoluteOffset - currentPosition : absoluteOffset + intelFileSize;
                writeU32(window, index, relativeOffset);
            }
            index += 4;
            currentPosition += 5;
        }
    }
    return window;
}

export function decompressRomLzx(stored) {
    const uncompressedSize = readU24(stored, 0);
    const blockCount = Math.ceil(uncompressedSize / romBlockSize);
    const blockTableEnd = 3 + 3 * blockCount;
    const blockEnds = Array.from({ length: blockCount }, (_, index) => readU24(stored, 3 + 3 * index));
    check(blockEnds.at(-1) === stored.length, 'Block table does not end at file length; not a CE ROM LZX file');
    const result = new Uint8Array(uncompressedSize);
    let blockStart = blockTableEnd;
    blockEnds.forEach((blockEnd, blockIndex) => {
        const wanted = Math.min(romBlockSize, uncompressedSize - blockIndex * romBlockSize);
        const block = stored.subarray(blockStart, blockEnd);
        if (block.length === wanted) {
            result.set(block, blockIndex * romBlockSize);
        } else {
            const windowBits = readU32(block, 0);
            const compressedSize = readU32(block, 8);
            const blockUncompressedSize = readU32(block, 12);
            check(compressedSize === block.length - 16, `Block ${blockIndex}: compressed size mismatch`);
            check(blockUncompressedSize === wanted, `Block ${blockIndex}: uncompressed size mismatch`);
            result.set(decompressLzxFrame(block.subarray(16), windowBits, wanted), blockIndex * romBlockSize);
        }
        blockStart = blockEnd;
    });
    return result;
}
