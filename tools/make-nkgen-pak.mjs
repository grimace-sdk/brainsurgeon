import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { basename, dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';
import { describePak, makePak, readPak } from './lib/nkgen-pak.mjs';
import { inputFiles, moduleReleases } from './lib/profiles.mjs';
import { parseWordPatch } from './lib/word-patch.mjs';

const usage = `Usage: node tools/make-nkgen-pak.mjs --build DIRECTORY [--output nkgen.pak] [--release VERSION] [--root DIRECTORY]
       node tools/make-nkgen-pak.mjs --describe nkgen.pak

Makes the generator inputs pak from profiles/profiles.json, tools/skip-modules.json, the fetched inputs/ (checked against their pinned SHA-256), binaries/ (checked against the SHA-256 in a README.md in the file's folder or a parent folder under binaries/) and the build output (bstart.exe, the CE programs, a7300-display.bin).
--root is the folder holding profiles/, tools/skip-modules.json, inputs/ and binaries/ (default: this repository).
--release defaults to git describe.
--describe prints a pak as text.`;

const { values: options } = parseArgs({
    options: {
        build: { type: 'string' },
        output: { type: 'string', default: 'nkgen.pak' },
        release: { type: 'string' },
        root: { type: 'string' },
        describe: { type: 'string' },
        help: { type: 'boolean', default: false },
    },
});
if (options.help || (!options.build && !options.describe)) {
    console.log(usage);
    process.exit(options.help ? 0 : 1);
}

if (options.describe) {
    process.stdout.write(describePak(readPak(new Uint8Array(readFileSync(options.describe)))));
    process.exit(0);
}

const repository = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const root = resolve(options.root ?? repository);
const build = resolve(options.build);
const definition = JSON.parse(readFileSync(join(root, 'profiles/profiles.json'), 'utf8'));
const skipList = JSON.parse(readFileSync(join(root, 'tools/skip-modules.json'), 'utf8'));
const release = options.release ?? execFileSync('git', ['-C', repository, 'describe', '--tags', '--always', '--dirty'], { encoding: 'utf8' }).trim();
const sha256 = (bytes) => createHash('sha256').update(bytes).digest('hex');

const inputPins = new Map(inputFiles(definition).map((file) => [file.path, file]));
const readmePins = (directory) => {
    const readme = join(root, directory, 'README.md');
    if (!existsSync(readme)) return new Map();
    const rows = readFileSync(readme, 'utf8').matchAll(/^\| `([^`]+)` \| (\d+) \| `([0-9a-f]{64})` \|/gm);
    return new Map([...rows].map(([, name, size, hash]) => [name, { size: Number(size), sha256: hash }]));
};
const binaryPin = (path) => {
    for (let directory = dirname(path); directory.startsWith('binaries'); directory = dirname(directory)) {
        const pins = readmePins(directory);
        const pin = pins.get(relative(directory, path)) ?? pins.get(basename(path));
        if (pin) return pin;
    }
    return undefined;
};

const locate = (path) => (path.startsWith('build/') ? join(build, path.slice('build/'.length)) : join(root, path));
const load = async (path) => {
    const location = locate(path);
    if (!existsSync(location)) throw new Error(`${location} is missing${path.startsWith('inputs/') ? '; run node tools/fetch-inputs.mjs' : ''}`);
    const bytes = new Uint8Array(readFileSync(location));
    const pin = inputPins.get(path) ?? (path.startsWith('binaries/') ? binaryPin(path) : undefined);
    if (path.startsWith('inputs/') || path.startsWith('binaries/')) {
        if (!pin) throw new Error(`${path} has no pinned SHA-256`);
        if (bytes.length !== pin.size || sha256(bytes) !== pin.sha256) throw new Error(`${path} does not match its pinned SHA-256`);
    }
    return bytes;
};

const pak = await makePak({
    definition,
    skipList,
    bstart: await load('build/bstart.exe'),
    release,
    load,
    releasesOf: (profile) => moduleReleases(definition, profile),
    parseWordPatch,
});
readPak(pak);
writeFileSync(options.output, pak);
console.log(`${options.output}: ${pak.length} bytes, release ${release}`);
