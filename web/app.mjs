import { sha256Hex } from './lib/bytes.mjs';
import { makeCard } from './lib/profiles.mjs';
import { makeZip } from './lib/zip.mjs';

const element = (id) => document.getElementById(id);
const drop = element('drop');
const imageInput = element('image-file');
const skipInput = element('skip');
const status = element('status');
const details = element('details');
const notesList = element('notes');
const downloadCard = element('download-card');
const cardHint = element('card-hint');
const cardList = element('card-files');

const readFile = async (path) => {
    const response = await fetch(path);
    if (!response.ok) throw new Error(`${path}: ${response.status}`);
    return new Uint8Array(await response.arrayBuffer());
};

const resources = Promise.all([
    readFile('build/bstart.exe'),
    fetch('skip-modules.json').then((response) => response.json()),
    fetch('profiles/profiles.json').then((response) => response.json()),
    fetch('build.txt').then((response) => (response.ok ? response.text() : '')),
]);

let currentFile = null;
let currentCard = null;
let skipChoice = 'auto';

const download = (name, contents) => {
    const link = document.createElement('a');
    link.href = URL.createObjectURL(new Blob([contents], { type: 'application/octet-stream' }));
    link.download = name;
    link.click();
    setTimeout(() => URL.revokeObjectURL(link.href), 1000);
};

const showStatus = (text, kind) => {
    status.textContent = text;
    status.className = `status ${kind}`;
};

const addDetail = (term, value) => {
    const termElement = document.createElement('dt');
    const valueElement = document.createElement('dd');
    termElement.textContent = term;
    valueElement.textContent = value;
    details.append(termElement, valueElement);
};

const megabytes = (bytes) => `${(bytes / 1048576).toFixed(1)} MB`;

const clearResult = () => {
    details.replaceChildren();
    notesList.replaceChildren();
    cardList.replaceChildren();
    cardHint.classList.add('hidden');
    downloadCard.classList.add('hidden');
    currentCard = null;
};

const makeFromCurrentFile = async () => {
    if (!currentFile) return;
    clearResult();
    showStatus(`Reading ${currentFile.name}...`, '');
    try {
        const [bstart, skipList, definition] = await resources;
        const contents = new Uint8Array(await currentFile.arrayBuffer());
        const made = await makeCard({ contents, bstart, skipList, skip: skipChoice, definition, readFile });
        currentCard = made.files;
        skipInput.checked = made.skipping;
        showStatus(made.notes.length ? 'Made, with notes below.' : 'Made. Everything matched.', made.notes.length ? 'warn' : 'good');
        addDetail('File', currentFile.name);
        if (made.slot) addDetail('Slot', made.slot);
        addDetail('Image', `${made.imageLength.toLocaleString()} bytes`);
        addDetail('SHA-256', made.imageSha256);
        addDetail('Device', made.device ? `${made.device}, ${made.imageDescription}` : 'not recognised: bstart only');
        addDetail('Patches', made.patches.join(', '));
        addDetail('Skipped', made.spans.length
            ? `${megabytes(made.skippedBytes)} in ${made.spans.length} spans (${Math.round(100 * made.skippedBytes / made.imageLength)}% of the image)`
            : 'nothing');
        for (const note of made.notes) {
            const item = document.createElement('li');
            item.textContent = note;
            notesList.append(item);
        }
        for (const file of currentCard) {
            const item = document.createElement('li');
            const name = document.createElement('code');
            name.textContent = file.path;
            item.append(name);
            cardList.append(item);
        }
        cardHint.classList.remove('hidden');
        downloadCard.classList.remove('hidden');
    } catch (error) {
        showStatus(`Could not make boot.nkp: ${error.message}`, 'bad');
    }
};

const chooseFile = (file) => {
    if (!file) return;
    currentFile = file;
    skipChoice = 'auto';
    makeFromCurrentFile();
};

imageInput.addEventListener('change', () => chooseFile(imageInput.files[0]));
skipInput.addEventListener('change', () => {
    skipChoice = skipInput.checked;
    makeFromCurrentFile();
});
drop.addEventListener('dragover', (event) => {
    event.preventDefault();
    drop.classList.add('active');
});
drop.addEventListener('dragleave', () => drop.classList.remove('active'));
drop.addEventListener('drop', (event) => {
    event.preventDefault();
    drop.classList.remove('active');
    chooseFile(event.dataTransfer.files[0]);
});
downloadCard.addEventListener('click', () => currentCard && download('brainsurgeon-card.zip', makeZip(currentCard)));

element('download-launch').addEventListener('click', () => {
    const lines = [
        `program=${element('program').value.trim()}`,
        `arguments=${element('arguments').value.trim()}`,
        `wait-for-window=${element('wait-for-window').value.trim()}`,
        `restart-keyboard=${element('restart-keyboard').checked ? 'yes' : 'no'}`,
        `restart-shortcut=${element('restart-shortcut').checked ? 'yes' : 'no'}`,
        `wallpaper=${element('plain-desktop').checked ? '' : 'stock'}`,
    ];
    download('launch.txt', lines.join('\r\n') + '\r\n');
});

resources.then(async ([bstart, , , build]) => {
    element('bstart-hash').textContent = await sha256Hex(bstart);
    element('build').textContent = build.trim();
}).catch((error) => {
    element('bstart-hash').textContent = 'unavailable';
    showStatus(`This page is missing its files: ${error.message}`, 'bad');
});
