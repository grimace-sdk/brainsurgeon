import { createHash } from 'node:crypto';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';
import { deflateSync } from 'node:zlib';

const usage = `Usage: node tools/make-icons.mjs [--fetch] [--check] [--preview PNG]

Renders the Material Symbols SVGs in ce/brain-control/icons/ into ce/brain-control/icons.h (8-bit alpha masks) and the
applet icon ce/brain-control/brain-control.ico, as listed in ce/brain-control/icons/icons.json.
--fetch downloads the SVGs again from the pinned commit. --check renders and fails if the checked-in outputs differ.
--preview writes a PNG contact sheet of every icon.`;

const { values: options } = parseArgs({
    options: {
        fetch: { type: 'boolean', default: false },
        check: { type: 'boolean', default: false },
        preview: { type: 'string' },
        help: { type: 'boolean', default: false },
    },
});
if (options.help) {
    console.log(usage);
    process.exit(0);
}

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const iconFolder = join(root, 'ce/brain-control/icons');
const manifest = JSON.parse(readFileSync(join(iconFolder, 'icons.json'), 'utf8'));
const sha256 = (bytes) => createHash('sha256').update(bytes).digest('hex');
const SAMPLES = 16;
const CURVE_SEGMENTS = 16;
const VIEWBOX_SIZE = 960;
const VIEWBOX_TOP = -960;

const sourceUrl = (name) => {
    const path = manifest.source.path.replaceAll('{name}', name);
    return `${manifest.source.repository.replace('https://github.com/', 'https://raw.githubusercontent.com/')}/${manifest.source.commit}/${path}`;
};

const readSymbol = (name) => {
    const path = join(iconFolder, `${name}.svg`);
    if (!existsSync(path)) throw new Error(`${path} is missing; run with --fetch`);
    const bytes = readFileSync(path);
    if (sha256(bytes) !== manifest.symbols[name]) throw new Error(`${name}.svg does not match its pinned SHA-256`);
    const text = bytes.toString('utf8');
    const viewBox = text.match(/viewBox="([^"]+)"/)?.[1];
    if (viewBox !== `0 ${VIEWBOX_TOP} ${VIEWBOX_SIZE} ${VIEWBOX_SIZE}`) throw new Error(`${name}.svg has viewBox ${viewBox}`);
    const paths = [...text.matchAll(/<path[^>]*\sd="([^"]+)"/g)].map((match) => match[1]);
    if (!paths.length) throw new Error(`${name}.svg has no path`);
    return paths.join(' ');
};

const tokenise = (data) => {
    const tokens = [];
    const pattern = /([MmLlHhVvCcSsQqTtAaZz])|([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)/g;
    for (const match of data.matchAll(pattern)) tokens.push(match[1] ?? Number(match[2]));
    return tokens;
};

const arcPoints = (start, rx, ry, rotationDegrees, largeArc, sweep, end) => {
    if (rx === 0 || ry === 0) return [end];
    const phi = rotationDegrees * Math.PI / 180;
    const cosPhi = Math.cos(phi);
    const sinPhi = Math.sin(phi);
    const halfX = (start[0] - end[0]) / 2;
    const halfY = (start[1] - end[1]) / 2;
    const x1 = cosPhi * halfX + sinPhi * halfY;
    const y1 = -sinPhi * halfX + cosPhi * halfY;
    let radiusX = Math.abs(rx);
    let radiusY = Math.abs(ry);
    const scale = (x1 * x1) / (radiusX * radiusX) + (y1 * y1) / (radiusY * radiusY);
    if (scale > 1) {
        radiusX *= Math.sqrt(scale);
        radiusY *= Math.sqrt(scale);
    }
    const numerator = radiusX * radiusX * radiusY * radiusY - radiusX * radiusX * y1 * y1 - radiusY * radiusY * x1 * x1;
    const denominator = radiusX * radiusX * y1 * y1 + radiusY * radiusY * x1 * x1;
    const factor = (largeArc === sweep ? -1 : 1) * Math.sqrt(Math.max(0, numerator / denominator));
    const centreX1 = factor * radiusX * y1 / radiusY;
    const centreY1 = -factor * radiusY * x1 / radiusX;
    const centreX = cosPhi * centreX1 - sinPhi * centreY1 + (start[0] + end[0]) / 2;
    const centreY = sinPhi * centreX1 + cosPhi * centreY1 + (start[1] + end[1]) / 2;
    const angle = (ux, uy, vx, vy) => {
        const sign = ux * vy - uy * vx < 0 ? -1 : 1;
        const dot = (ux * vx + uy * vy) / (Math.hypot(ux, uy) * Math.hypot(vx, vy));
        return sign * Math.acos(Math.min(1, Math.max(-1, dot)));
    };
    const startAngle = angle(1, 0, (x1 - centreX1) / radiusX, (y1 - centreY1) / radiusY);
    let sweepAngle = angle((x1 - centreX1) / radiusX, (y1 - centreY1) / radiusY, (-x1 - centreX1) / radiusX, (-y1 - centreY1) / radiusY);
    if (!sweep && sweepAngle > 0) sweepAngle -= 2 * Math.PI;
    if (sweep && sweepAngle < 0) sweepAngle += 2 * Math.PI;
    const segments = Math.max(4, Math.ceil(Math.abs(sweepAngle) / (Math.PI / 16)));
    const points = [];
    for (let index = 1; index <= segments; index++) {
        const theta = startAngle + sweepAngle * index / segments;
        const x = radiusX * Math.cos(theta);
        const y = radiusY * Math.sin(theta);
        points.push(index === segments ? end : [cosPhi * x - sinPhi * y + centreX, sinPhi * x + cosPhi * y + centreY]);
    }
    return points;
};

const flatten = (data) => {
    const tokens = tokenise(data);
    const contours = [];
    let contour = null;
    let current = [0, 0];
    let start = [0, 0];
    let lastControl = null;
    let lastCommand = '';
    let command = '';
    let index = 0;
    const number = () => {
        const value = tokens[index++];
        if (typeof value !== 'number') throw new Error(`Expected a number in path at token ${index - 1}`);
        return value;
    };
    const lineTo = (point) => {
        if (!contour) {
            contour = [current];
            contours.push(contour);
        }
        contour.push(point);
        current = point;
    };
    const quadratic = (control, end) => {
        const from = current;
        for (let step = 1; step <= CURVE_SEGMENTS; step++) {
            const t = step / CURVE_SEGMENTS;
            const u = 1 - t;
            lineTo(step === CURVE_SEGMENTS ? end : [u * u * from[0] + 2 * u * t * control[0] + t * t * end[0], u * u * from[1] + 2 * u * t * control[1] + t * t * end[1]]);
        }
    };
    const cubic = (first, second, end) => {
        const from = current;
        for (let step = 1; step <= CURVE_SEGMENTS; step++) {
            const t = step / CURVE_SEGMENTS;
            const u = 1 - t;
            const a = u * u * u;
            const b = 3 * u * u * t;
            const c = 3 * u * t * t;
            const d = t * t * t;
            lineTo(step === CURVE_SEGMENTS ? end : [a * from[0] + b * first[0] + c * second[0] + d * end[0], a * from[1] + b * first[1] + c * second[1] + d * end[1]]);
        }
    };
    while (index < tokens.length) {
        if (typeof tokens[index] === 'string') command = tokens[index++];
        else if (!command) throw new Error('Path does not start with a command');
        const relative = command === command.toLowerCase();
        const offset = (x, y) => (relative ? [current[0] + x, current[1] + y] : [x, y]);
        const reflected = () => (lastControl ? [2 * current[0] - lastControl[0], 2 * current[1] - lastControl[1]] : current);
        let control = null;
        switch (command.toUpperCase()) {
        case 'M': {
            const point = offset(number(), number());
            contour = [point];
            contours.push(contour);
            current = point;
            start = point;
            command = relative ? 'l' : 'L';
            break;
        }
        case 'L':
            lineTo(offset(number(), number()));
            break;
        case 'H': {
            const x = number();
            lineTo([relative ? current[0] + x : x, current[1]]);
            break;
        }
        case 'V': {
            const y = number();
            lineTo([current[0], relative ? current[1] + y : y]);
            break;
        }
        case 'C': {
            const first = offset(number(), number());
            const second = offset(number(), number());
            const end = offset(number(), number());
            cubic(first, second, end);
            control = second;
            break;
        }
        case 'S': {
            const first = 'CS'.includes(lastCommand) ? reflected() : current;
            const second = offset(number(), number());
            const end = offset(number(), number());
            cubic(first, second, end);
            control = second;
            break;
        }
        case 'Q': {
            const point = offset(number(), number());
            const end = offset(number(), number());
            quadratic(point, end);
            control = point;
            break;
        }
        case 'T': {
            const point = 'QT'.includes(lastCommand) ? reflected() : current;
            quadratic(point, offset(number(), number()));
            control = point;
            break;
        }
        case 'A': {
            const rx = number();
            const ry = number();
            const rotation = number();
            const largeArc = number() !== 0;
            const sweep = number() !== 0;
            const end = offset(number(), number());
            for (const point of arcPoints(current, rx, ry, rotation, largeArc, sweep, end)) lineTo(point);
            break;
        }
        case 'Z':
            if (contour) contour.push(start);
            current = start;
            contour = null;
            break;
        default:
            throw new Error(`Unsupported path command ${command}`);
        }
        lastCommand = command.toUpperCase();
        lastControl = control;
    }
    return contours;
};

const rasterise = (contours, size, transform) => {
    const edges = [];
    for (const contour of contours) {
        for (let index = 1; index < contour.length; index++) {
            const [x0, y0] = transform(contour[index - 1]);
            const [x1, y1] = transform(contour[index]);
            if (y0 === y1) continue;
            edges.push(y0 < y1 ? { top: y0, bottom: y1, x: x0, slope: (x1 - x0) / (y1 - y0), winding: 1 }
                : { top: y1, bottom: y0, x: x1, slope: (x0 - x1) / (y0 - y1), winding: -1 });
        }
    }
    const counts = new Uint32Array(size * size);
    const columns = size * SAMPLES;
    for (let row = 0; row < size * SAMPLES; row++) {
        const y = (row + 0.5) / SAMPLES;
        const crossings = [];
        for (const edge of edges) if (y >= edge.top && y < edge.bottom) crossings.push({ x: edge.x + (y - edge.top) * edge.slope, winding: edge.winding });
        crossings.sort((first, second) => first.x - second.x);
        let winding = 0;
        for (let index = 0; index < crossings.length - 1; index++) {
            winding += crossings[index].winding;
            if (!winding) continue;
            const first = Math.max(0, Math.ceil(crossings[index].x * SAMPLES - 0.5));
            const last = Math.min(columns, Math.ceil(crossings[index + 1].x * SAMPLES - 0.5));
            const pixelRow = Math.floor(row / SAMPLES) * size;
            for (let column = first; column < last; column++) counts[pixelRow + Math.floor(column / SAMPLES)]++;
        }
    }
    return Uint8Array.from(counts, (count) => Math.round(count * 255 / (SAMPLES * SAMPLES)));
};

const renderSymbol = (name, size) => {
    const scale = size / VIEWBOX_SIZE;
    return rasterise(flatten(readSymbol(name)), size, ([x, y]) => [x * scale, (y - VIEWBOX_TOP) * scale]);
};

const roundedSquare = (size) => {
    const inset = size / 32;
    const radius = size / 5;
    const low = inset;
    const high = size - inset;
    const path = `M${low + radius} ${low}H${high - radius}A${radius} ${radius} 0 0 1 ${high} ${low + radius}V${high - radius}`
        + `A${radius} ${radius} 0 0 1 ${high - radius} ${high}H${low + radius}A${radius} ${radius} 0 0 1 ${low} ${high - radius}`
        + `V${low + radius}A${radius} ${radius} 0 0 1 ${low + radius} ${low}Z`;
    return rasterise(flatten(path), size, (point) => point);
};

const appIconImage = (size) => {
    const { symbol, glyphScale, top, bottom } = manifest.appIcon;
    const background = roundedSquare(size);
    const glyphSize = Math.round(size * glyphScale);
    const glyph = renderSymbol(symbol, glyphSize);
    const offset = Math.floor((size - glyphSize) / 2);
    const pixels = new Uint8Array(size * size * 4);
    for (let y = 0; y < size; y++) {
        const mix = y / (size - 1);
        const colour = top.map((value, channel) => Math.round(value + (bottom[channel] - value) * mix));
        for (let x = 0; x < size; x++) {
            const glyphX = x - offset;
            const glyphY = y - offset;
            const amount = glyphX >= 0 && glyphX < glyphSize && glyphY >= 0 && glyphY < glyphSize ? glyph[glyphY * glyphSize + glyphX] : 0;
            const pixel = (y * size + x) * 4;
            const opaque = background[y * size + x] >= 128;
            for (let channel = 0; channel < 3; channel++) pixels[pixel + channel] = opaque ? Math.round(colour[channel] + (255 - colour[channel]) * amount / 255) : 0;
            pixels[pixel + 3] = opaque ? 255 : 0;
        }
    }
    return { size, pixels };
};

const icoFile = (images) => {
    const entries = images.map(({ size, pixels }) => {
        const maskStride = Math.ceil(size / 32) * 4;
        const entry = Buffer.alloc(40 + size * size * 4 + maskStride * size);
        entry.writeUInt32LE(40, 0);
        entry.writeInt32LE(size, 4);
        entry.writeInt32LE(size * 2, 8);
        entry.writeUInt16LE(1, 12);
        entry.writeUInt16LE(32, 14);
        entry.writeUInt32LE(size * size * 4 + maskStride * size, 20);
        let offset = 40;
        for (let y = size - 1; y >= 0; y--) {
            for (let x = 0; x < size; x++) {
                const pixel = (y * size + x) * 4;
                entry[offset++] = pixels[pixel + 2];
                entry[offset++] = pixels[pixel + 1];
                entry[offset++] = pixels[pixel];
                entry[offset++] = pixels[pixel + 3];
            }
        }
        for (let y = size - 1; y >= 0; y--) {
            for (let x = 0; x < size; x++) if (!pixels[(y * size + x) * 4 + 3]) entry[offset + (x >> 3)] |= 0x80 >> (x & 7);
            offset += maskStride;
        }
        return entry;
    });
    const directory = Buffer.alloc(6 + 16 * entries.length);
    directory.writeUInt16LE(1, 2);
    directory.writeUInt16LE(entries.length, 4);
    let offset = directory.length;
    entries.forEach((entry, index) => {
        const size = images[index].size;
        const base = 6 + 16 * index;
        directory[base] = size;
        directory[base + 1] = size;
        directory.writeUInt16LE(1, base + 4);
        directory.writeUInt16LE(32, base + 6);
        directory.writeUInt32LE(entry.length, base + 8);
        directory.writeUInt32LE(offset, base + 12);
        offset += entry.length;
    });
    return Buffer.concat([directory, ...entries]);
};

const header = (masks) => {
    const lines = ['#ifndef BRAIN_CONTROL_ICONS_H', '#define BRAIN_CONTROL_ICONS_H', '', '#include <windows.h>', '',
        'struct brain_icon {', '    int size;', '    const BYTE *alpha;', '};', ''];
    for (const { name, size, alpha } of masks) {
        lines.push(`static const BYTE icon_${name}_${size}_alpha[${size * size}] = {`);
        for (let row = 0; row < size; row++) lines.push(`    ${Array.from(alpha.subarray(row * size, (row + 1) * size)).join(', ')},`);
        lines.push('};', `static const struct brain_icon icon_${name}_${size} = { ${size}, icon_${name}_${size}_alpha };`, '');
    }
    lines.push('#endif');
    return `${lines.join('\n')}\n`;
};

const crcTable = Array.from({ length: 256 }, (_, value) => {
    let crc = value;
    for (let bit = 0; bit < 8; bit++) crc = crc & 1 ? 0xedb88320 ^ (crc >>> 1) : crc >>> 1;
    return crc >>> 0;
});
const crc32 = (bytes) => {
    let crc = 0xffffffff;
    for (const byte of bytes) crc = crcTable[(crc ^ byte) & 0xff] ^ (crc >>> 8);
    return (crc ^ 0xffffffff) >>> 0;
};
const pngChunk = (type, data) => {
    const body = Buffer.concat([Buffer.from(type, 'latin1'), data]);
    const length = Buffer.alloc(4);
    length.writeUInt32BE(data.length);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(crc32(body));
    return Buffer.concat([length, body, crc]);
};
const pngFile = (width, height, rgb) => {
    const information = Buffer.alloc(13);
    information.writeUInt32BE(width, 0);
    information.writeUInt32BE(height, 4);
    information[8] = 8;
    information[9] = 2;
    const rows = Buffer.alloc((width * 3 + 1) * height);
    for (let y = 0; y < height; y++) rgb.copy(rows, y * (width * 3 + 1) + 1, y * width * 3, (y + 1) * width * 3);
    return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), pngChunk('IHDR', information),
        pngChunk('IDAT', deflateSync(rows)), pngChunk('IEND', Buffer.alloc(0))]);
};

const preview = (masks, appImages, path) => {
    const zoom = 4;
    const gap = 8;
    const tiles = [...masks.map((mask) => ({ size: mask.size, colour: (x, y) => { const value = 40 + Math.round(mask.alpha[y * mask.size + x] * 215 / 255); return [value, value, value]; } })),
        ...appImages.map((image) => ({ size: image.size, colour: (x, y) => { const pixel = (y * image.size + x) * 4; return image.pixels[pixel + 3] ? [image.pixels[pixel], image.pixels[pixel + 1], image.pixels[pixel + 2]] : [40, 40, 40]; } }))];
    const width = tiles.reduce((total, tile) => total + tile.size * zoom + gap, gap);
    const height = Math.max(...tiles.map((tile) => tile.size)) * zoom + gap * 2;
    const rgb = Buffer.alloc(width * height * 3, 90);
    let left = gap;
    for (const tile of tiles) {
        for (let y = 0; y < tile.size * zoom; y++) {
            for (let x = 0; x < tile.size * zoom; x++) {
                const [red, green, blue] = tile.colour(Math.floor(x / zoom), Math.floor(y / zoom));
                const pixel = ((gap + y) * width + left + x) * 3;
                rgb[pixel] = red;
                rgb[pixel + 1] = green;
                rgb[pixel + 2] = blue;
            }
        }
        left += tile.size * zoom + gap;
    }
    writeFileSync(path, pngFile(width, height, rgb));
};

if (options.fetch) {
    for (const [name, pinned] of Object.entries(manifest.symbols)) {
        const response = await fetch(sourceUrl(name));
        if (!response.ok) throw new Error(`${sourceUrl(name)}: ${response.status}`);
        const bytes = Buffer.from(await response.arrayBuffer());
        if (sha256(bytes) !== pinned) throw new Error(`${name}.svg from ${manifest.source.commit} has SHA-256 ${sha256(bytes)}, pinned ${pinned}`);
        writeFileSync(join(iconFolder, `${name}.svg`), bytes);
    }
}

const masks = manifest.sections.flatMap((name) => manifest.sizes.map((size) => ({ name, size, alpha: renderSymbol(name, size) })));
const appImages = manifest.appIcon.sizes.map(appIconImage);
const outputs = [
    { path: join(root, manifest.header), contents: Buffer.from(header(masks)) },
    { path: join(root, manifest.appIcon.output), contents: icoFile(appImages) },
];
if (options.preview) preview(masks, appImages, options.preview);
if (options.check) {
    const stale = outputs.filter((output) => !existsSync(output.path) || !readFileSync(output.path).equals(output.contents));
    for (const output of stale) console.error(`${output.path} does not match tools/make-icons.mjs; run it again`);
    process.exit(stale.length ? 1 : 0);
}
for (const output of outputs) writeFileSync(output.path, output.contents);
console.log(`wrote ${outputs.map((output) => output.path.slice(root.length + 1)).join(', ')}`);
