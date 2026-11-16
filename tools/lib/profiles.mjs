import { check, sha256Hex } from './bytes.mjs';
import { makeBootNkp } from './boot-nkp.mjs';
import { redirectImports } from './pe-imports.mjs';
import { extractRomDll, patchBytes } from './rom-dll.mjs';
import { imageFromDumpOrSlot, openImage } from './rom-image.mjs';
import { parseWordPatch } from './word-patch.mjs';

function inputFiles(definition) {
    return definition.inputs.flatMap((input) => input.members
        ? Object.entries(input.members).map(([name, member]) => ({ path: `inputs/${input.directory}/${name}`, ...member }))
        : [{ path: `inputs/${input.file}`, sha256: input.sha256, size: input.size }]);
}

export function findProfile(definition, imageSha256) {
    for (const profile of definition.profiles) {
        const image = profile.images.find((candidate) => candidate.sha256 === imageSha256);
        if (image) return { profile, image, device: image.device ?? profile.device };
    }
    return null;
}

const releaseOf = (definition, path) => {
    const input = definition.inputs.find((candidate) => path === `inputs/${candidate.file}` || (candidate.directory && path.startsWith(`inputs/${candidate.directory}/`)));
    const release = input?.url.match(/^https:\/\/github\.com\/[^/]+\/([^/]+)\/releases\/download\/([^/]+)\//);
    return release ? `${release[1]} ${release[2]}` : null;
};

const registryString = (text) => `"${text.replaceAll('\\', '\\\\').replaceAll('"', '\\"')}"`;

const modelRegistry = (definition, match) => {
    const lines = [
        '[HKEY_LOCAL_MACHINE\\Software\\Brain]',
        `"Model"=${registryString(match.device)}`,
        `"Profile"=${registryString(match.profile.id)}`,
        `"Image"=${registryString(match.image.description)}`,
    ];
    const releases = (match.profile.modules ?? []).filter((entry) => entry.file).map((entry) => [entry.name, releaseOf(definition, entry.file)]).filter(([, release]) => release);
    if (releases.length) lines.push('[HKEY_LOCAL_MACHINE\\Software\\Brain\\Modules]', ...releases.map(([name, release]) => `${registryString(name)}=${registryString(release)}`));
    return lines.join('\n');
};

export const bundleFolder = (imageLength) => `pwsh2-boot/nk-${imageLength.toString(16).padStart(8, '0')}`;

const deriveFromRom = (image, rom) => {
    let bytes = extractRomDll(image, rom.module, { spareSections: rom.spareSections ?? 0 });
    for (const redirect of rom.redirectImports ?? []) bytes = redirectImports(bytes, redirect.from, redirect.to, redirect.imports);
    if (rom.patchBytes) bytes = patchBytes(bytes, rom.patchBytes, rom.module);
    return bytes;
};

export async function makeCard({ contents, slot, bstart, skipList, skip = 'auto', definition, readFile, useProfile = true, extra = {} }) {
    const source = imageFromDumpOrSlot(contents, slot);
    const imageSha256 = await sha256Hex(source.bytes);
    const match = useProfile ? findProfile(definition, imageSha256) : null;
    const notes = [];
    const applied = match && skip !== false;
    if (match && !applied) notes.push(`${match.device} drivers need skipping, so only bstart was added`);

    const expected = new Map(inputFiles(definition).map((file) => [file.path, file]));
    const decoder = new TextDecoder();
    const load = async (path) => {
        const bytes = await readFile(path);
        const pin = expected.get(path);
        if (pin) check(bytes.length === pin.size && await sha256Hex(bytes) === pin.sha256, `${path} does not match its pinned SHA-256; run tools/fetch-inputs.mjs again`);
        return bytes;
    };

    const profile = applied ? match.profile : {};
    const image = applied ? openImage(source.bytes) : null;
    const produce = async (entry) => (entry.rom ? deriveFromRom(image, entry.rom) : load(entry.file));
    const profileModules = [];
    for (const entry of profile.modules ?? []) profileModules.push({ name: entry.name, contents: await produce(entry) });
    const extraModules = extra.moduleReplacements ?? [];
    const sameName = (first) => (second) => first.name.toLowerCase() === second.name.toLowerCase();
    const moduleReplacements = [
        ...profileModules.map((module) => extraModules.find(sameName(module)) ?? module),
        ...extraModules.filter((module) => !profileModules.some(sameName(module))),
    ];
    const registryTexts = [];
    for (const path of profile.registry ?? []) registryTexts.push(decoder.decode(await load(path)));
    if (applied) registryTexts.push(modelRegistry(definition, match));
    registryTexts.push(...(extra.registryAdditions ?? []));
    const wordPatches = [];
    for (const path of profile.wordPatches ?? []) wordPatches.push(parseWordPatch(decoder.decode(await load(path))));
    wordPatches.push(...(extra.wordPatches ?? []));
    const a7300Display = extra.a7300Display ?? (profile.a7300Display ? await load(profile.a7300Display) : null);

    const result = await makeBootNkp({
        contents: source.bytes,
        bstart,
        skipList,
        skip: applied ? true : skip,
        a7300Display,
        debugLog: extra.debugLog ?? null,
        debugLogAddress: extra.debugLogAddress,
        moduleReplacements,
        registryAdditions: registryTexts.length ? registryTexts.join('\n') : null,
        wordPatches,
    });

    const folder = bundleFolder(result.imageLength);
    const files = [
        { path: `${folder}/boot.nkp`, contents: new TextEncoder().encode(result.bundle) },
        ...result.replacedModules.map((module) => ({ path: `${folder}/${module.cardPath}`, contents: module.contents })),
    ];
    for (const entry of profile.cardFiles ?? []) files.push({ path: entry.path, contents: await produce(entry) });

    return {
        ...result,
        slot: source.slot,
        notes: [...notes, ...result.notes],
        device: match?.device ?? null,
        profile: applied ? match.profile.id : null,
        imageDescription: match?.image.description ?? null,
        folder,
        files,
    };
}
