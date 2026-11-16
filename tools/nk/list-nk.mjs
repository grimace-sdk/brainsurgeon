import { readFileSync } from 'node:fs';
import {
    freeGap, hex, listCopyEntries, listFiles, listModules, openImage, romHeaderFieldNames,
} from '../lib/rom-image.mjs';

const usage = 'Usage: node tools/nk/list-nk.mjs IMAGE [--json]';
const argumentsList = process.argv.slice(2);
const json = argumentsList.includes('--json');
const [imagePath] = argumentsList.filter((argument) => argument !== '--json');
if (!imagePath) {
    console.error(usage);
    process.exit(1);
}

const image = openImage(new Uint8Array(readFileSync(imagePath)));
const { header } = image;
const copies = listCopyEntries(image);
const gap = freeGap(image);
const modules = listModules(image);
const files = listFiles(image);

if (json) {
    console.log(JSON.stringify({ imageBytes: image.bytes.length, header, copies, freeGap: gap, modules, files }, null, 2));
    process.exit(0);
}

const row = (columns, widths) => columns.map((column, index) => String(column).padEnd(widths[index])).join('  ').trimEnd();
const printTable = (title, headings, rows) => {
    const widths = headings.map((heading, index) => Math.max(heading.length, ...rows.map((columns) => String(columns[index]).length)));
    console.log(`\n${title}`);
    console.log(row(headings, widths));
    for (const columns of rows) console.log(row(columns, widths));
};

console.log(`Image bytes ${image.bytes.length} (${hex(image.bytes.length)})`);
console.log(`ROMHDR at VA ${hex(header.va)}, image offset ${hex(header.offset)}`);
for (const name of romHeaderFieldNames()) console.log(`  ${name.padEnd(16)} ${hex(header[name])}`);

printTable('Copy entries', ['#', 'source', 'src offset', 'destination', 'copy len', 'dest len'],
    copies.map((copy) => [copy.index, hex(copy.source), hex(copy.sourceOffset), hex(copy.destination), hex(copy.copyLength), hex(copy.destinationLength)]));

console.log(`\nFree gap physlast ${hex(gap.from)} to ulRAMStart ${hex(gap.to)}: ${gap.bytes} bytes (${hex(gap.bytes)})`);

printTable('Modules', ['#', 'name', 'toc off', 'e32 off', 'o32 off', 'flags', 'vbase', 'vsize', 'sections (rva:offset+psize[c])'],
    modules.map((module) => [
        module.index, module.name, hex(module.tocOffset), hex(module.e32Offset), hex(module.o32Offset), hex(module.imageFlags, 4),
        hex(module.virtualBase), hex(module.virtualSize),
        module.sections.map((section) => `${hex(section.rva, 5)}:${section.dataOffset === null ? '-' : hex(section.dataOffset)}+${hex(section.physicalSize, 0)}${section.compressed ? 'c' : ''}`).join(' '),
    ]));

printTable('Files', ['#', 'name', 'attrs', 'real', 'stored', 'load VA', 'image off', 'compressed'],
    files.map((file) => [file.index, file.name, hex(file.attributes), file.realSize, file.compressedSize, hex(file.loadVa), hex(file.loadOffset), file.compressed ? 'yes' : 'no']));
