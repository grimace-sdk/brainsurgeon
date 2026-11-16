import { readFileSync } from 'node:fs';

const definition = JSON.parse(readFileSync(new URL('../profiles/profiles.json', import.meta.url), 'utf8'));

if (process.argv[2] === '--card') {
    const cardFiles = new Map();
    const entries = [...definition.profiles.flatMap((profile) => profile.cardFiles ?? []), ...definition.sharedCardFiles ?? []];
    for (const entry of entries) if (entry.file) cardFiles.set(entry.path, entry.file);
    console.log([...cardFiles].sort(([first], [second]) => first.localeCompare(second)).map(([path, file]) => `${path}\t${file}`).join('\n'));
} else {
    const paths = new Set(['build/bstart.exe', 'profiles/profiles.json']);
    for (const entry of definition.sharedCardFiles ?? []) if (entry.file) paths.add(entry.file);
    for (const profile of definition.profiles) {
        if (profile.a7300Display) paths.add(profile.a7300Display);
        for (const entry of [...profile.modules ?? [], ...profile.cardFiles ?? []]) if (entry.file) paths.add(entry.file);
        for (const path of [...profile.registry ?? [], ...profile.wordPatches ?? []]) paths.add(path);
    }
    console.log([...paths].sort().join('\n'));
}
