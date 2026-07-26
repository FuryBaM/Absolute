'use strict';

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const lang = require('../server/language');
const toolchain = require('../toolchain');

const repo = path.resolve(__dirname, '..', '..');
const metadata = path.join(repo, 'plugins', 'desktop', 'absolute-desktop.editor.json');
const sourceFile = path.join(repo, 'examples', 'desktop', 'd3d-triangle.abs');
const index = lang.loadEditorIndex([], [metadata]);
const duplicateIndex = lang.loadEditorIndex([metadata], [metadata]);
assert.strictEqual(
    duplicateIndex.entries.filter(entry => entry.fullName === 'Desktop.Window').length,
    1,
    'duplicate editor sidecars must not duplicate completion entries'
);

function positionOf(text, needle, occurrence = 0, inside = 1) {
    let offset = -1;
    for (let index = 0; index <= occurrence; index += 1)
        offset = text.indexOf(needle, offset + 1);
    assert(offset >= 0, `missing test token: ${needle}`);
    const offsets = lang.lineOffsets(text);
    return lang.offsetToPosition(offsets, offset + inside);
}

const sample = [
    'int32 main() {',
    '    auto window = new Desktop.Window("Demo", 800, 450, true);',
    '    auto gpu = new Desktop.Gpu(window, Desktop.Gpu.BackendAuto);',
    '    window.poll();',
    '    gpu.clear(0.1, 0.2, 0.3, 1.0);',
    '}'
].join('\n');

const windowHover = lang.hoverFor(index, sample, positionOf(sample, 'window.poll', 0, 2));
assert(windowHover.contents.value.includes('Desktop.Window* window'));
assert(windowHover.contents.value.includes('automatic cleanup'));

const pollHover = lang.hoverFor(index, sample, positionOf(sample, 'poll', 0, 2));
assert(pollHover.contents.value.includes('bool poll()'));
assert(pollHover.contents.value.includes('Desktop.Window'));

const windowCompletion = lang.completionItems(
    index,
    `${sample}\nwindow.`,
    positionOf(`${sample}\nwindow.`, 'window.', 1, 'window.'.length)
);
assert(windowCompletion.some(item => item.label === 'poll'));
assert(!windowCompletion.some(item => item.label === 'createShader'));

const signatureText = `${sample}\nwindow.fillRect(10, 20, `;
const signature = lang.signatureHelpFor(
    index,
    signatureText,
    lang.offsetToPosition(lang.lineOffsets(signatureText), signatureText.length)
);
assert(signature);
assert.strictEqual(signature.signatures[0].label,
    'void fillRect(int32 x, int32 y, int32 width, int32 height, uint32 color)');
assert.strictEqual(signature.activeParameter, 2);

const roots = toolchain.candidateRoots([repo]);
const compiler = toolchain.resolveCompilerPath('auto', roots);
assert(compiler.path, 'workspace compiler should be auto-detected');
const discovered = toolchain.discoverPlugins(roots);
assert(discovered.desktop, 'desktop plugin manifest should be auto-detected');
const selected = toolchain.selectPluginsForSource(sourceFile, discovered, []);
assert(selected.includes(discovered.desktop), 'Desktop source should select the desktop plugin');

const diagnostics = lang.runCompilerDiagnostics(
    sourceFile,
    compiler.path,
    toolchain.pluginArguments(selected)
);
assert.deepStrictEqual(diagnostics, []);

const parsed = JSON.parse(fs.readFileSync(metadata, 'utf8'));
assert.strictEqual(parsed.plugin, 'absolute.desktop');

const preludeRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'absolute-prelude-'));
try {
    const preludeFile = path.join(preludeRoot, 'api.abs');
    const manifestFile = path.join(preludeRoot, 'file-prelude.absplugin');
    fs.writeFileSync(preludeFile, [
        'namespace FilePrelude {',
        '    int32 answer() { return 42; }',
        '}'
    ].join('\n'));
    fs.writeFileSync(manifestFile, JSON.stringify({
        name: 'file.prelude',
        version: '1.0.0',
        abi: 1,
        library: 'unused-for-editor.dll',
        prelude: 'api.abs'
    }, null, 2));
    const preludeIndex = lang.loadEditorIndex([manifestFile], []);
    assert(preludeIndex.namespaceNames.has('FilePrelude'));
    assert(preludeIndex.functionNames.has('answer'));
    const answerHover = lang.hoverFor(
        preludeIndex,
        'FilePrelude.answer();',
        positionOf('FilePrelude.answer();', 'answer', 0, 2)
    );
    assert(answerHover.contents.value.includes('api.abs'));
} finally {
    fs.rmSync(preludeRoot, { recursive: true, force: true });
}

console.log('absolute-extension language/toolchain smoke: OK');
