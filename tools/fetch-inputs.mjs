import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { inflateRawSync } from 'node:zlib';

const usage = `Usage: node tools/fetch-inputs.mjs [DIRECTORY]

Downloads the release assets profiles/profiles.json pins into DIRECTORY (default inputs/), checking each SHA-256, and extracts the listed files from zips.`;

if (process.argv.includes('--help')) {
    console.log(usage);
    process.exit(0);
}

const repository = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const output = resolve(process.argv[2] ?? join(repository, 'inputs'));
const definition = JSON.parse(readFileSync(join(repository, 'profiles/profiles.json'), 'utf8'));
const token = process.env.GITHUB_TOKEN || process.env.GH_TOKEN;
const sha256 = (bytes) => createHash('sha256').update(bytes).digest('hex');

const download = async (url) => {
    const release = url.match(/^https:\/\/github\.com\/([^/]+)\/([^/]+)\/releases\/download\/([^/]+)\/([^/]+)$/);
    if (!token || !release) {
        const response = await fetch(url);
        if (!response.ok) throw new Error(`${url}: ${response.status}`);
        return Buffer.from(await response.arrayBuffer());
    }
    const [, owner, name, tag, asset] = release;
    const headers = { Authorization: `Bearer ${token}`, 'X-GitHub-Api-Version': '2022-11-28' };
    const listing = await fetch(`https://api.github.com/repos/${owner}/${name}/releases/tags/${tag}`, { headers });
    if (!listing.ok) throw new Error(`${owner}/${name} ${tag}: ${listing.status}`);
    const found = (await listing.json()).assets.find((candidate) => candidate.name === asset);
    if (!found) throw new Error(`${owner}/${name} ${tag} has no ${asset}`);
    const response = await fetch(found.url, { headers: { ...headers, Accept: 'application/octet-stream' } });
    if (!response.ok) throw new Error(`${url}: ${response.status}`);
    return Buffer.from(await response.arrayBuffer());
};

const unzip = (zip) => {
    const end = zip.lastIndexOf(Buffer.from([0x50, 0x4b, 0x05, 0x06]));
    if (end < 0) throw new Error('No zip end of central directory');
    const entries = new Map();
    let offset = zip.readUInt32LE(end + 16);
    for (let index = 0; index < zip.readUInt16LE(end + 10); index++) {
        if (zip.readUInt32LE(offset) !== 0x02014b50) throw new Error('Bad zip central directory');
        const method = zip.readUInt16LE(offset + 10);
        const compressedSize = zip.readUInt32LE(offset + 20);
        const nameLength = zip.readUInt16LE(offset + 28);
        const extraLength = zip.readUInt16LE(offset + 30);
        const commentLength = zip.readUInt16LE(offset + 32);
        const localHeader = zip.readUInt32LE(offset + 42);
        const name = zip.toString('utf8', offset + 46, offset + 46 + nameLength);
        const dataStart = localHeader + 30 + zip.readUInt16LE(localHeader + 26) + zip.readUInt16LE(localHeader + 28);
        const stored = zip.subarray(dataStart, dataStart + compressedSize);
        if (method !== 0 && method !== 8) throw new Error(`${name}: zip method ${method} is not supported`);
        entries.set(name, method === 8 ? inflateRawSync(stored) : stored);
        offset += 46 + nameLength + extraLength + commentLength;
    }
    return entries;
};

const matches = (path, pin) => existsSync(path) && (() => {
    const bytes = readFileSync(path);
    return bytes.length === pin.size && sha256(bytes) === pin.sha256;
})();

const check = (bytes, pin, label) => {
    if (bytes.length !== pin.size || sha256(bytes) !== pin.sha256) throw new Error(`${label}: ${bytes.length} bytes, sha256 ${sha256(bytes)}; expected ${pin.size} bytes, sha256 ${pin.sha256}`);
};

for (const input of definition.inputs) {
    const targets = input.members
        ? Object.entries(input.members).map(([name, pin]) => ({ name, pin, path: join(output, input.directory, name) }))
        : [{ name: input.file, pin: input, path: join(output, input.file) }];
    if (targets.every((target) => matches(target.path, target.pin))) {
        console.log(`${input.url}: present`);
        continue;
    }
    const bytes = await download(input.url);
    check(bytes, input, input.url);
    const contents = input.members ? unzip(bytes) : new Map([[input.file, bytes]]);
    for (const target of targets) {
        const member = contents.get(target.name);
        if (!member) throw new Error(`${input.url} has no ${target.name}`);
        check(member, target.pin, `${input.url} ${target.name}`);
        mkdirSync(dirname(target.path), { recursive: true });
        writeFileSync(target.path, member);
        console.log(`${target.path}: ${target.pin.sha256}`);
    }
}
