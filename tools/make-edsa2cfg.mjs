import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';

const usage = 'Usage: node tools/make-edsa2cfg.mjs OUTPUT DEVELOP_WORD [CONFIG_FLAGS]';
assert.ok(process.argv[2] && process.argv[3], usage);
const output = process.argv[2];
const developWord = Number(process.argv[3]) >>> 0;
const configFlags = Number(process.argv[4] ?? 0x20) >>> 0;

const fileSize = 512;
const keyword = 'SHARP E-DICTIONARY BOOT CONFIG';
const checksumOffset = 0x20;
const flagsOffset = 0x24;
const developWordOffset = 0x28;
const checksummedLength = 0x2c;

const allowedFlags = 0x22;
assert.equal(configFlags & ~allowedFlags, 0, 'Only flag bits 0x20 (use dev mode) and 0x02 (boot the card U-Boot) are permitted');
if (configFlags & 0x20) {
    assert.ok(developWord & 0x80000000, 'Develop word bit 31 must stay set to avoid the EBOOT dev-download branch');
    assert.ok(developWord & 0x10, 'Develop word bit 0x10 must be set for VMCopy to honour flags');
    assert.ok(developWord & 0x08000000, 'Develop word bit 0x08000000 must stay set or battdrvr disables charging');
    assert.equal(developWord & 0x30000000, 0x30000000, 'Develop word bits 0x30000000 must stay set to match stock OAL/EdAppCtrl behaviour');
    assert.equal(developWord & 0x740, 0x740, 'Develop word bits 0x40/0x100/0x200/0x400 must stay set to keep keyboard, battery and power manager stock');
} else {
    assert.equal(developWord, 0, 'Without flag bit 0x20 the develop word is unused and must be 0');
}

const config = Buffer.alloc(fileSize);
config.write(keyword, 0, 'latin1');
config.writeUInt32LE(configFlags, flagsOffset);
config.writeUInt32LE(developWord, developWordOffset);

const bytesSum = (buffer) => {
    let sum = 0;
    for (let index = 0; index < checksummedLength; index++) sum = (sum + buffer[index]) & 0xffff;
    return sum;
};
const low = (bytesSum(config) + 0x1fe) & 0xffff;
config.writeUInt32LE(((0xffff - low) << 16 | low) >>> 0, checksumOffset);

const verify = bytesSum(config);
const stored = config.readUInt32LE(checksumOffset);
assert.equal(stored & 0xffff, verify, 'Checksum low half mismatch');
assert.equal(stored >>> 16, (0xffff - verify) & 0xffff, 'Checksum high half mismatch');
assert.equal(config.toString('latin1', 0, keyword.length + 1), keyword + '\0');

writeFileSync(output, config);
console.log(JSON.stringify({
    output,
    bytes: config.length,
    config_flags: `0x${configFlags.toString(16)}`,
    develop_word: `0x${developWord.toString(16)}`,
    checksum: `0x${stored.toString(16).padStart(8, '0')}`,
}, null, 2));
