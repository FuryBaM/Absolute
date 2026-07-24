'use strict';

const vscode = require('vscode');
const fs = require('fs');
const path = require('path');
const language = require('./server/language');

const SCHEME = 'absolute-opaque';
const cache = new Map(); // virtualUri string -> { body, language, hostPath, hostLine, tag, name }

function hostFileUri(fsPath) {
    return vscode.Uri.file(fsPath).toString();
}

function refreshFromDocument(document) {
    if (!document || document.languageId !== 'absolute') return [];
    const hostUri = document.uri.toString();
    const blocks = language.extractOpaqueBlocks(hostUri, document.getText(), ['shader']);
    for (const block of blocks) {
        cache.set(block.virtualUri, {
            body: block.body,
            language: block.language,
            hostPath: language.uriToPath(block.hostUri),
            hostLine: block.hostBreakpointLine,
            tag: block.tag,
            name: block.name,
            hostRange: block.hostRange
        });
    }
    return blocks;
}

function refreshFromPath(filePath) {
    if (!filePath || !fs.existsSync(filePath)) return [];
    const text = fs.readFileSync(filePath, 'utf8');
    const hostUri = hostFileUri(filePath);
    const blocks = language.extractOpaqueBlocks(hostUri, text, ['shader']);
    for (const block of blocks) {
        cache.set(block.virtualUri, {
            body: block.body,
            language: block.language,
            hostPath: filePath,
            hostLine: block.hostBreakpointLine,
            tag: block.tag,
            name: block.name,
            hostRange: block.hostRange
        });
    }
    return blocks;
}

function provideTextDocumentContent(uri) {
    const key = uri.toString();
    const hit = cache.get(key);
    if (hit) return hit.body;
    const parsed = language.parseOpaqueVirtualUri(key);
    if (!parsed) return `// Unknown opaque virtual document\n// ${key}\n`;
    refreshFromPath(parsed.hostPath);
    const again = cache.get(key);
    return again ? again.body : `// Opaque block not found in ${parsed.hostPath}\n`;
}

async function openOpaqueBlockAtCursor() {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== 'absolute') {
        vscode.window.showErrorMessage('Absolute: open a .abs file with an opaque block.');
        return;
    }
    const blocks = refreshFromDocument(editor.document);
    if (!blocks.length) {
        vscode.window.showInformationMessage('Absolute: no opaque plugin blocks (e.g. shader) in this file.');
        return;
    }
    const line = editor.selection.active.line;
    let block = blocks.find(b =>
        line >= b.hostRange.start.line && line <= b.hostRange.end.line);
    if (!block) {
        const pick = await vscode.window.showQuickPick(
            blocks.map(b => ({
                label: `${b.tag}${b.name ? ' ' + b.name : ''}`,
                description: `line ${b.hostRange.start.line + 1}`,
                block: b
            })),
            { placeHolder: 'Select opaque block' }
        );
        block = pick && pick.block;
    }
    if (!block) return;
    const uri = vscode.Uri.parse(block.virtualUri);
    const doc = await vscode.workspace.openTextDocument(uri);
    await vscode.window.showTextDocument(doc, { preview: false, viewColumn: vscode.ViewColumn.Beside });
}

function mapBreakpointFromVirtual(virtualUri, line) {
    const key = typeof virtualUri === 'string' ? virtualUri : virtualUri.toString();
    let hit = cache.get(key);
    if (!hit) {
        const parsed = language.parseOpaqueVirtualUri(key);
        if (parsed) {
            refreshFromPath(parsed.hostPath);
            hit = cache.get(key);
        }
    }
    if (!hit) return undefined;
    // Map virtual line 0..n onto host block start (coarse mapping for opaque bodies).
    return {
        hostPath: hit.hostPath,
        hostLine: hit.hostLine,
        tag: hit.tag,
        name: hit.name
    };
}

function register(context) {
    const provider = {
        provideTextDocumentContent(uri) {
            return provideTextDocumentContent(uri);
        }
    };
    context.subscriptions.push(
        vscode.workspace.registerTextDocumentContentProvider(SCHEME, provider),
        vscode.commands.registerCommand('absolute.openOpaqueBlock', openOpaqueBlockAtCursor),
        vscode.workspace.onDidSaveTextDocument(doc => {
            if (doc.languageId === 'absolute') refreshFromDocument(doc);
        })
    );
}

module.exports = {
    SCHEME,
    register,
    openOpaqueBlockAtCursor,
    mapBreakpointFromVirtual,
    refreshFromDocument,
    refreshFromPath,
    cache
};
