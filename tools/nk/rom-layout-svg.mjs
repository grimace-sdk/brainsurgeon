import { readFileSync, writeFileSync } from 'node:fs';
import { parseArgs } from 'node:util';
import { check, hex, sha256Hex } from '../lib/bytes.mjs';
import { findProfile } from '../lib/profiles.mjs';
import { fileTableVa, filesEntrySize, imageBase, listFiles, listModules, moduleTableVa, openImage, romHeaderSize, tocEntrySize } from '../lib/rom-image.mjs';

const usage = `Usage: node tools/nk/rom-layout-svg.mjs --image NK_IMAGE --bundle boot.nkp --output LAYOUT.svg [--title TEXT]

Draws NK_IMAGE as boot.nkp leaves it in RAM: the skipped spans and the Sharp modules they held, the modules loaded from the card into them, renamed ROM modules, and what is appended past the image end. The bar is to scale; the inset of the image end is not.
boot.nkp is the one tools/make-boot-nkp.mjs made for NK_IMAGE. Its patches are applied, after zero-filling the skipped spans, and each is checked as U-Boot checks it.
--title defaults to the device and image description in profiles/profiles.json.`;

const { values: options } = parseArgs({
    options: {
        image: { type: 'string' },
        bundle: { type: 'string' },
        output: { type: 'string' },
        title: { type: 'string' },
        help: { type: 'boolean', default: false },
    },
});
if (options.help || !options.image || !options.bundle || !options.output) {
    console.log(usage);
    process.exit(options.help ? 0 : 1);
}

const skippedGroups = [
    { pattern: /^SharpLib\.dll$/i, label: 'Sharp library' },
    { pattern: /^(EdAppCtrl\.exe|DiagApp\.exe|copydll\.dll|EdFileJPN\.dll)$/i, label: 'Sharp apps' },
    { pattern: /^(TTS\.exe|vt_\w+\.dll)$/i, label: 'Text to speech' },
    { pattern: /^XMDFViewerDLL\.dll$/i, label: 'XMDF book viewer' },
    { pattern: /^HWR/i, label: 'Handwriting recognition' },
    { pattern: /^(dbapi2ed|edjdb|edw\w+|EntierDal)\.dll$/i, label: 'Dictionary engine' },
];

const colours = {
    background: '#ffffff',
    border: '#d0d7de',
    text: '#1f2328',
    muted: '#59636e',
    kept: '#c8d3df',
    keptEdge: '#8c9bab',
    skipped: '#fbe2bb',
    skippedStripe: '#d9902f',
    ours: '#1f6feb',
    oursFill: '#ddebff',
    renamed: '#8250df',
    table: '#57606a',
    free: '#f6f8fa',
};

const parseHex = (text) => {
    check(/^[0-9a-f]+$/i.test(text), `${text} is not hex`);
    return Number.parseInt(text, 16);
};
const hexBytes = (text) => Uint8Array.from(text.match(/../g), (pair) => Number.parseInt(pair, 16));

const parseBundle = (text) => {
    const spans = [];
    const sections = [];
    const current = () => {
        check(sections.length, 'boot.nkp has records before its first patch line');
        return sections.at(-1);
    };
    for (const line of text.split('\n').map((entry) => entry.trim()).filter(Boolean)) {
        const words = line.split(/\s+/);
        if (words[0] === 'skip') spans.push({ start: parseHex(words[1]), end: parseHex(words[2]), imageLength: parseHex(words[3]) });
        else if (words[0] === 'patch') sections.push({ name: line.slice('patch'.length).trim(), loads: [], records: [] });
        else if (words[0] === 'image') {
            const section = current();
            section.imageLength = parseHex(words[1]);
            section.extendedLength = words[2] ? parseHex(words[2]) : section.imageLength;
        } else if (words[0] === 'load') current().loads.push({ offset: parseHex(words[1]), length: parseHex(words[2]), path: words[4] });
        else current().records.push({ offset: parseHex(words[0]), expected: hexBytes(words[1]), replacement: hexBytes(words[2]) });
    }
    return { spans, sections };
};

const applyBundle = (bytes, bundle) => {
    for (const item of [...bundle.spans, ...bundle.sections]) check(item.imageLength === bytes.length, `boot.nkp is for an image of ${item.imageLength} bytes, not ${bytes.length}`);
    const patched = new Uint8Array(Math.max(bytes.length, ...bundle.sections.map((section) => section.extendedLength)));
    patched.set(bytes);
    for (const span of bundle.spans) patched.fill(0, span.start, span.end);
    for (const section of bundle.sections) {
        for (const record of section.records) {
            const matches = record.offset + record.expected.length <= patched.length
                && record.expected.every((byte, index) => patched[record.offset + index] === byte);
            check(matches, `${section.name}: bytes at ${hex(record.offset)} differ from boot.nkp; it was made for another image`);
            patched.set(record.replacement, record.offset);
        }
    }
    return patched;
};

const formatSize = (bytes) => {
    if (bytes >= 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
    if (bytes >= 1024) return `${Math.round(bytes / 1024)} KB`;
    return `${bytes} B`;
};

const escapeXml = (text) => String(text).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');

const wrapWords = (words, maximumCharacters, separator = ', ') => {
    const lines = [];
    for (const word of words) {
        const last = lines.at(-1);
        if (last !== undefined && last.length + separator.length + word.length <= maximumCharacters) lines[lines.length - 1] = `${last}${separator}${word}`;
        else lines.push(word);
    }
    return lines;
};

const spreadLabels = (labels, top, bottom, gap) => {
    const sorted = [...labels].sort((first, second) => first.wantedY - second.wantedY);
    let next = top;
    for (const label of sorted) {
        label.top = Math.max(label.wantedY - label.height / 2, next);
        next = label.top + label.height + gap;
    }
    let limit = bottom;
    for (const label of [...sorted].reverse()) {
        if (label.top + label.height > limit) label.top = limit - label.height;
        limit = label.top - gap;
    }
    return sorted;
};

const bytes = new Uint8Array(readFileSync(options.image));
const bundle = parseBundle(readFileSync(options.bundle, 'utf8'));
const patched = applyBundle(bytes, bundle);
const original = openImage(bytes);
const result = openImage(patched);
const { header } = original;

const definition = JSON.parse(readFileSync(new URL('../../profiles/profiles.json', import.meta.url), 'utf8'));
const match = findProfile(definition, await sha256Hex(bytes));
const title = options.title ?? (match ? `${match.device} ${match.image.description}: the CE image as boot.nkp leaves it` : 'CE image as boot.nkp leaves it');

const originalModules = listModules(original);
const resultModules = listModules(result);
const originalFiles = listFiles(original);
const resultFiles = listFiles(result);
const firstSectionOffset = (module) => module.sections.find((section) => section.dataOffset !== null)?.dataOffset ?? 0;

const renamedModules = originalModules
    .filter((module) => resultModules[module.index].name !== module.name)
    .map((module) => ({ name: module.name, renamed: resultModules[module.index].name, offset: firstSectionOffset(module) }));
const newFiles = resultFiles.slice(originalFiles.length);

const describeLoad = (file) => {
    if (file.index < originalFiles.length) return "Sharp's file entry repointed";
    const replaced = renamedModules.find((module) => module.name.toLowerCase() === file.name.toLowerCase());
    return replaced ? `ROM module renamed ${replaced.renamed}` : 'new ROM file';
};

const loaded = bundle.sections.flatMap((section) => section.loads.map((load) => {
    const file = resultFiles.find((candidate) => candidate.loadOffset === load.offset);
    check(file, `No ROM file entry points at the ${load.path} load`);
    return { name: file.name, offset: load.offset, length: load.length, how: describeLoad(file) };
}));

const skipListed = new Set(JSON.parse(readFileSync(new URL('../skip-modules.json', import.meta.url), 'utf8')).modules.map((name) => name.toLowerCase()));
const skippedRanges = originalModules
    .filter((module) => skipListed.has(module.name.toLowerCase()))
    .flatMap((module) => module.sections
        .filter((section) => section.dataOffset !== null)
        .map((section) => ({ name: module.name, start: section.dataOffset, end: section.dataOffset + Math.min(section.physicalSize, section.virtualSize || section.physicalSize) })));
const groupOf = (name) => skippedGroups.find((group) => group.pattern.test(name))?.label ?? 'Dictionary only';

const spanGroups = [];
for (const span of bundle.spans) {
    const overlaps = skippedRanges
        .map((range) => ({ name: range.name, bytes: Math.min(range.end, span.end) - Math.max(range.start, span.start) }))
        .filter((overlap) => overlap.bytes > 0);
    check(overlaps.length, `Skip span ${hex(span.start)} holds no skip-listed module`);
    const largest = overlaps.reduce((best, overlap) => (overlap.bytes > best.bytes ? overlap : best));
    const label = groupOf(largest.name);
    const last = spanGroups.at(-1);
    const group = last && last.label === label ? last : { label, start: span.start, end: span.end, bytes: 0, names: [] };
    if (group !== last) spanGroups.push(group);
    group.end = span.end;
    group.bytes += span.end - span.start;
    for (const overlap of overlaps) if (!group.names.includes(overlap.name)) group.names.push(overlap.name);
}

const filesByOffset = [...originalFiles].sort((first, second) => second.loadOffset - first.loadOffset);
let filesStart = header.offset;
const fileBlock = [];
for (const file of filesByOffset) {
    if (file.loadOffset < filesStart && file.loadOffset + file.compressedSize >= filesStart - 16) {
        filesStart = file.loadOffset;
        fileBlock.unshift(file);
    }
}

const width = 900;
const barX = 360;
const barWidth = 64;
const barTop = 128;
const barHeight = 820;
const barBottom = barTop + barHeight;
const tickX = barX + barWidth + 8;
const bracketX = tickX + 76;
const labelX = bracketX + 16;
const boxX = 24;
const boxWidth = 290;
const scale = barHeight / bytes.length;
const yOf = (offset) => barTop + offset * scale;
const sans = 'system-ui, -apple-system, Segoe UI, Helvetica, Arial, sans-serif';
const mono = 'ui-monospace, SFMono-Regular, Menlo, Consolas, monospace';
const lineHeight = 14;
const elements = [];
const add = (text) => elements.push(text);
const textElement = (x, y, content, { size = 12, weight = 'normal', fill = colours.text, anchor = 'start', family = sans } = {}) => add(
    `<text x="${x}" y="${y}" font-family="${family}" font-size="${size}" font-weight="${weight}" fill="${fill}" text-anchor="${anchor}">${escapeXml(content)}</text>`,
);

const vaText = (offset) => hex(imageBase + offset);

add(`<rect x="${barX}" y="${barTop}" width="${barWidth}" height="${barHeight}" fill="${colours.kept}" stroke="${colours.keptEdge}"/>`);
for (const span of bundle.spans) {
    add(`<rect x="${barX}" y="${yOf(span.start).toFixed(2)}" width="${barWidth}" height="${Math.max((span.end - span.start) * scale, 0.5).toFixed(2)}" fill="url(#skipped)"/>`);
}
for (const module of renamedModules) {
    add(`<rect x="${barX}" y="${(yOf(module.offset) - 1).toFixed(2)}" width="${barWidth}" height="2" fill="${colours.renamed}"/>`);
}
for (const item of loaded) {
    add(`<rect x="${barX}" y="${yOf(item.offset).toFixed(2)}" width="${barWidth}" height="${Math.max(item.length * scale, 2.5).toFixed(2)}" fill="${colours.ours}"/>`);
}
add(`<rect x="${barX}" y="${barTop}" width="${barWidth}" height="${barHeight}" fill="none" stroke="${colours.keptEdge}"/>`);

const ticks = spreadLabels([
    { offset: 0 },
    { offset: bundle.spans[0]?.start ?? 0 },
    { offset: bundle.spans.at(-1)?.end ?? 0 },
    { offset: filesStart },
    { offset: bytes.length },
].map((tick) => ({ ...tick, wantedY: yOf(tick.offset), height: 12 })), barTop - 6, barBottom + 6, 2);
for (const tick of ticks) {
    add(`<line x1="${barX + barWidth}" y1="${yOf(tick.offset).toFixed(2)}" x2="${tickX - 2}" y2="${(tick.top + 6).toFixed(2)}" stroke="${colours.muted}" stroke-width="0.75"/>`);
    textElement(tickX, tick.top + 10, vaText(tick.offset), { size: 10, family: mono, fill: colours.muted });
}

const rightLabels = [];
const firstSkip = bundle.spans[0]?.start ?? bytes.length;
const coreModules = originalModules.filter((module) => firstSectionOffset(module) < firstSkip);
const fixedModules = ['nk.exe', 'kernel.dll'].map((name) => originalModules.find((module) => module.name.toLowerCase() === name)).filter(Boolean);
rightLabels.push({
    wantedY: yOf(0) + 14,
    point: yOf(0),
    lines: [
        { text: fixedModules.map((module) => `${module.name} ${hex(module.virtualBase)}`).join(', '), weight: 'bold' },
        { text: 'run in place from fixed addresses', fill: colours.muted, size: 11 },
    ],
});
rightLabels.push({
    wantedY: (yOf(0) + yOf(firstSkip)) / 2,
    bracket: [0, firstSkip],
    lines: [
        { text: `Kept: ${coreModules.length} modules`, weight: 'bold' },
        { text: 'CE kernel, drivers and shell', fill: colours.muted, size: 11 },
    ],
});
for (const module of renamedModules) {
    rightLabels.push({
        wantedY: yOf(module.offset),
        point: yOf(module.offset),
        colour: colours.renamed,
        lines: [{ text: `${module.name} renamed ${module.renamed}`, fill: colours.renamed, size: 11 }],
    });
}
for (const group of spanGroups) {
    rightLabels.push({
        wantedY: (yOf(group.start) + yOf(group.end)) / 2,
        bracket: [group.start, group.end],
        colour: colours.skippedStripe,
        lines: [
            { text: `Skipped: ${group.label}, ${formatSize(group.bytes)}`, weight: 'bold' },
            ...wrapWords(group.names, 46).map((text) => ({ text, fill: colours.muted, size: 11 })),
        ],
    });
}
rightLabels.push({
    wantedY: (yOf(filesStart) + yOf(header.offset)) / 2,
    bracket: [filesStart, header.offset],
    lines: [
        { text: `Kept: ${fileBlock.length} ROM files`, weight: 'bold' },
        { text: `${fileBlock.slice(1, 4).map((file) => file.name).join(', ')}, ...`, fill: colours.muted, size: 11 },
    ],
});
rightLabels.push({
    wantedY: barBottom,
    point: barBottom,
    lines: [
        { text: 'ROM header, module and file tables', weight: 'bold' },
        { text: 'then appended entries: see inset', fill: colours.muted, size: 11 },
    ],
});
for (const label of rightLabels) label.height = label.lines.length * lineHeight + 2;
for (const label of spreadLabels(rightLabels, barTop - 10, barBottom + 40, 6)) {
    const colour = label.colour ?? colours.keptEdge;
    let anchorY = label.point;
    if (label.bracket) {
        const top = yOf(label.bracket[0]);
        const bottom = Math.max(yOf(label.bracket[1]), top + 1.5);
        add(`<path d="M ${bracketX - 6} ${top.toFixed(2)} H ${bracketX} V ${bottom.toFixed(2)} H ${bracketX - 6}" fill="none" stroke="${colour}" stroke-width="1.5"/>`);
        anchorY = (top + bottom) / 2;
        add(`<line x1="${bracketX}" y1="${anchorY.toFixed(2)}" x2="${labelX - 4}" y2="${(label.top + 8).toFixed(2)}" stroke="${colour}" stroke-width="0.75"/>`);
    } else {
        add(`<line x1="${barX + barWidth}" y1="${anchorY.toFixed(2)}" x2="${labelX - 4}" y2="${(label.top + 8).toFixed(2)}" stroke="${colour}" stroke-width="0.75" stroke-dasharray="2 2"/>`);
    }
    label.lines.forEach((line, index) => textElement(labelX, label.top + 11 + index * lineHeight, line.text, { size: line.size ?? 12, weight: line.weight, fill: line.fill }));
}

const oursHeading = barTop - 10;
textElement(boxX, oursHeading, 'Loaded from the card into skipped spans', { weight: 'bold' });
const explanation = [
    'U-Boot zero-fills each skipped span, then reads our',
    'files from the card into them. Each gets a ROM file',
    'entry, new or repointed. Where Sharp\'s ROM has a',
    'module of the same name, it is renamed, so CE',
    'loads ours.',
];
explanation.forEach((line, index) => textElement(boxX, oursHeading + 22 + index * lineHeight, line, { size: 12, fill: colours.muted }));
const boxes = loaded.map((item) => ({ ...item, wantedY: yOf(item.offset), height: 2 * lineHeight + 10 }));
const placedBoxes = spreadLabels(boxes, oursHeading + 22 + explanation.length * lineHeight, Number.POSITIVE_INFINITY, 8);
for (const box of placedBoxes) {
    add(`<rect x="${boxX}" y="${box.top.toFixed(2)}" width="${boxWidth}" height="${box.height}" rx="4" fill="${colours.oursFill}" stroke="${colours.ours}"/>`);
    textElement(boxX + 8, box.top + 16, `${box.name}`, { weight: 'bold' });
    textElement(boxX + boxWidth - 8, box.top + 16, `${formatSize(box.length)} at ${vaText(box.offset)}`, { size: 10, family: mono, fill: colours.muted, anchor: 'end' });
    textElement(boxX + 8, box.top + 16 + lineHeight, box.how, { size: 11, fill: box.how.startsWith('ROM module') ? colours.renamed : colours.muted });
    add(`<path d="M ${boxX + boxWidth} ${(box.top + box.height / 2).toFixed(2)} C ${boxX + boxWidth + 24} ${(box.top + box.height / 2).toFixed(2)}, ${barX - 24} ${yOf(box.offset).toFixed(2)}, ${barX - 1} ${yOf(box.offset).toFixed(2)}" fill="none" stroke="${colours.ours}" stroke-width="1.25" marker-end="url(#arrow)"/>`);
}

const appendedRows = [];
let previousEnd = bytes.length;
const entriesEnd = bytes.length + newFiles.filter((file) => file.entryOffset >= bytes.length).length * filesEntrySize;
for (const section of bundle.sections) {
    if (section.extendedLength <= previousEnd) continue;
    if (previousEnd === bytes.length) {
        appendedRows.push({
            kind: 'ours',
            offset: bytes.length,
            lines: [
                { text: `File table: ${newFiles.length} more entries`, weight: 'bold' },
                ...wrapWords(newFiles.map((file) => file.name), 50).map((text) => ({ text, fill: colours.muted, size: 11 })),
            ],
        });
        for (const file of newFiles.filter((candidate) => candidate.loadOffset >= bytes.length)) {
            appendedRows.push({
                kind: 'ours',
                offset: entriesEnd,
                lines: [{ text: file.name, weight: 'bold' }, { text: `name and data, ${formatSize(file.realSize)}`, fill: colours.muted, size: 11 }],
            });
        }
    } else {
        appendedRows.push({ kind: 'ours', offset: previousEnd, lines: [{ text: `patch ${section.name}: ${formatSize(section.extendedLength - previousEnd)}`, weight: 'bold' }] });
    }
    previousEnd = section.extendedLength;
}

const insetRows = [
    {
        kind: 'kept',
        offset: filesStart,
        lines: [
            { text: 'ROM files', weight: 'bold' },
            { text: `${fileBlock[0].name} to ${fileBlock.at(-1).name}`, fill: colours.muted, size: 11 },
        ],
    },
    {
        kind: 'table',
        offset: header.offset,
        lines: [
            { text: `ROM header, ${romHeaderSize} B`, weight: 'bold' },
            { text: `numfiles ${header.numfiles} to ${result.header.numfiles}, physlast moved`, fill: colours.muted, size: 11 },
        ],
    },
    { kind: 'table', offset: moduleTableVa(original) - imageBase, lines: [{ text: `Module table: ${header.nummods} x ${tocEntrySize} B`, weight: 'bold' }] },
    { kind: 'table', offset: fileTableVa(original) - imageBase, lines: [{ text: `File table: ${header.numfiles} x ${filesEntrySize} B`, weight: 'bold' }] },
    { kind: 'marker', offset: bytes.length, text: `image end, physlast ${vaText(bytes.length)}` },
    ...appendedRows,
    { kind: 'marker', offset: result.header.physlast - imageBase, text: `new physlast ${hex(result.header.physlast)}` },
    { kind: 'free', offset: result.header.physlast - imageBase, lines: [{ text: `free up to ulRAMStart ${hex(header.ulRAMStart)}`, fill: colours.muted, size: 11 }] },
];

const insetTop = Math.max(placedBoxes.length ? placedBoxes.at(-1).top + placedBoxes.at(-1).height + 56 : 0, barTop + 420);
textElement(boxX, insetTop - 10, 'Image end, not to scale', { weight: 'bold' });
let rowY = insetTop;
const rowFill = { kept: colours.kept, table: '#e3e7eb', ours: colours.oursFill, free: colours.free };
const rowStroke = { kept: colours.keptEdge, table: colours.table, ours: colours.ours, free: colours.border };
for (const row of insetRows) {
    if (row.kind === 'marker') {
        add(`<line x1="${boxX - 6}" y1="${rowY}" x2="${boxX + boxWidth + 6}" y2="${rowY}" stroke="${colours.text}" stroke-width="1.5" stroke-dasharray="5 3"/>`);
        textElement(boxX + boxWidth, rowY + 12, row.text, { size: 10, family: mono, anchor: 'end' });
        rowY += 18;
        continue;
    }
    const height = row.lines.length * lineHeight + 10;
    add(`<rect x="${boxX}" y="${rowY}" width="${boxWidth}" height="${height}" fill="${rowFill[row.kind]}" stroke="${rowStroke[row.kind]}"/>`);
    row.lines.forEach((line, index) => textElement(boxX + 8, rowY + 16 + index * lineHeight, line.text, { size: line.size ?? 12, weight: line.weight, fill: line.fill }));
    if (row.kind !== 'free') textElement(boxX + boxWidth - 6, rowY + 16, vaText(row.offset), { size: 10, family: mono, fill: colours.muted, anchor: 'end' });
    rowY += height;
}
const insetBottom = rowY;
const zoomTop = yOf(filesStart);
add(`<path d="M ${barX} ${zoomTop.toFixed(2)} L ${boxX + boxWidth + 8} ${insetTop} M ${barX} ${barBottom} L ${boxX + boxWidth + 8} ${insetBottom}" fill="none" stroke="${colours.muted}" stroke-width="0.75" stroke-dasharray="3 3"/>`);
add(`<path d="M ${barX - 4} ${zoomTop.toFixed(2)} H ${barX - 8} V ${barBottom} H ${barX - 4}" fill="none" stroke="${colours.muted}" stroke-width="1"/>`);

const legendItems = [
    { label: 'Kept', fill: colours.kept, stroke: colours.keptEdge },
    { label: 'Skipped: not read, zero-filled', fill: 'url(#skipped)', stroke: colours.skippedStripe },
    { label: 'Ours, loaded from the card', fill: colours.ours, stroke: colours.ours },
    { label: 'Renamed ROM module', fill: colours.renamed, stroke: colours.renamed },
];
let legendX = boxX;
const legendY = 76;
for (const item of legendItems) {
    add(`<rect x="${legendX}" y="${legendY - 10}" width="14" height="12" fill="${item.fill}" stroke="${item.stroke}"/>`);
    textElement(legendX + 20, legendY, item.label, { size: 12 });
    legendX += 20 + item.label.length * 6.6 + 24;
}

const height = Math.ceil(Math.max(insetBottom, barBottom + 40) + 28);
textElement(boxX, 34, title, { size: 17, weight: 'bold' });
textElement(boxX, 54, `${bytes.length.toLocaleString('en-GB')} bytes at ${hex(imageBase)}. Bar to scale, 1 px = ${formatSize(Math.round(1 / scale))}. ${bundle.spans.length} skipped spans, ${formatSize(bundle.spans.reduce((total, span) => total + span.end - span.start, 0))}.`, { size: 12, fill: colours.muted });
textElement(barX + barWidth / 2, barTop - 8, 'Sharp CE image', { size: 11, anchor: 'middle', fill: colours.muted });

const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}">
<defs>
<pattern id="skipped" width="6" height="6" patternUnits="userSpaceOnUse" patternTransform="rotate(45)"><rect width="6" height="6" fill="${colours.skipped}"/><rect width="1.5" height="6" fill="${colours.skippedStripe}"/></pattern>
<marker id="arrow" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M 0 0 L 8 4 L 0 8 z" fill="${colours.ours}"/></marker>
</defs>
<rect width="${width}" height="${height}" rx="8" fill="${colours.background}" stroke="${colours.border}"/>
${elements.join('\n')}
</svg>
`;
writeFileSync(options.output, svg);
console.log(`${options.output}: ${bundle.spans.length} skip spans in ${spanGroups.length} groups, ${loaded.length} loaded modules, ${renamedModules.length} renamed, ${newFiles.length} new file entries`);
