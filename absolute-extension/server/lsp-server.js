'use strict';

/**
 * Zero-dependency Absolute Language Server Protocol server (stdio).
 * Implements completion, hover, definition, references, rename, symbols,
 * formatting, semantic tokens, and compiler diagnostics.
 */

const fs = require('fs');
const path = require('path');
const lang = require('./language');

const documents = new Map(); // uri -> { text, version }
let workspaceFolders = [];
let pluginIndex = lang.emptyPluginIndex();
let workspaceSymbols = new Map();
let compilerPath = 'absolutec';
let compilerArguments = [];
let initialized = false;

function writeMessage(message) {
    const json = JSON.stringify(message);
    const payload = `Content-Length: ${Buffer.byteLength(json, 'utf8')}\r\n\r\n${json}`;
    process.stdout.write(payload);
}

function respond(id, result) {
    writeMessage({ jsonrpc: '2.0', id, result });
}

function respondError(id, code, message) {
    writeMessage({ jsonrpc: '2.0', id, error: { code, message } });
}

function notify(method, params) {
    writeMessage({ jsonrpc: '2.0', method, params });
}

function log(message) {
    notify('window/logMessage', { type: 3, message: String(message) });
}

function refreshWorkspaceIndex() {
    const roots = workspaceFolders.length
        ? workspaceFolders.map(folder => lang.uriToPath(folder.uri || folder))
        : [];
    workspaceSymbols = lang.buildWorkspaceSymbolIndex(roots);
}

function publishDiagnostics(uri) {
    const doc = documents.get(uri);
    if (!doc) return;
    const filePath = lang.uriToPath(uri);
    const diagnostics = lang.runCompilerDiagnostics(filePath, compilerPath, compilerArguments);
    notify('textDocument/publishDiagnostics', { uri, diagnostics });
}

function documentText(uri) {
    const doc = documents.get(uri);
    if (doc) return doc.text;
    const filePath = lang.uriToPath(uri);
    if (!filePath || !fs.existsSync(filePath)) return undefined;
    try { return fs.readFileSync(filePath, 'utf8'); }
    catch (_) { return undefined; }
}

function documentSymbols(uri) {
    const text = documentText(uri);
    if (text === undefined) return [];
    return lang.extractSymbols(uri, text).map(symbol => ({
        name: symbol.name,
        detail: symbol.detail,
        kind: symbol.kind,
        range: symbol.range,
        selectionRange: symbol.selectionRange
    }));
}

function definition(uri, position) {
    const doc = documents.get(uri);
    if (!doc) return null;
    const word = lang.wordAt(doc.text, position);
    if (!word) return null;

    // Prefer local symbols first.
    const local = lang.extractSymbols(uri, doc.text).filter(s => s.name === word.name);
    if (local.length) {
        return local.map(s => ({ uri: s.uri, range: s.selectionRange }));
    }

    const remote = workspaceSymbols.get(word.name) || [];
    if (!remote.length) return null;
    return remote.map(s => ({ uri: s.uri, range: s.selectionRange }));
}

function references(uri, position, includeDeclaration = true) {
    const doc = documents.get(uri);
    if (!doc) return [];
    const word = lang.wordAt(doc.text, position);
    if (!word) return [];

    const results = [];
    const roots = workspaceFolders.length
        ? workspaceFolders.map(folder => lang.uriToPath(folder.uri || folder))
        : [path.dirname(lang.uriToPath(uri))];

    const files = new Set();
    for (const root of roots) for (const file of lang.walkAbsFiles(root)) files.add(file);
    files.add(lang.uriToPath(uri));

    for (const file of files) {
        let text = '';
        const fileUri = lang.pathToUri(file);
        if (documents.has(fileUri)) text = documents.get(fileUri).text;
        else {
            try { text = fs.readFileSync(file, 'utf8'); }
            catch (_) { continue; }
        }
        for (const ref of lang.findReferencesInText(fileUri, text, word.name)) {
            results.push(ref);
        }
    }

    if (!includeDeclaration) {
        // Keep all text matches; declaration filter is best-effort only.
    }
    return results;
}

function rename(uri, position, newName) {
    if (!newName || !/^[A-Za-z_][A-Za-z0-9_]*$/.test(newName)) {
        throw new Error('Invalid Absolute identifier');
    }
    const refs = references(uri, position, true);
    const changes = {};
    for (const ref of refs) {
        if (!changes[ref.uri]) changes[ref.uri] = [];
        changes[ref.uri].push({ range: ref.range, newText: newName });
    }
    return { changes };
}

function formatting(uri, options) {
    const doc = documents.get(uri);
    if (!doc) return [];
    const formatted = lang.formatDocument(doc.text, options || {});
    if (formatted === doc.text) return [];
    const offsets = lang.lineOffsets(doc.text);
    const end = lang.offsetToPosition(offsets, doc.text.length);
    return [{
        range: { start: { line: 0, character: 0 }, end },
        newText: formatted
    }];
}

function codeActions(uri, range) {
    return [
        {
            title: 'Format document',
            kind: 'source.formatDocument',
            command: {
                title: 'Format document',
                command: 'absolute.languageServer.formatDocument',
                arguments: [uri]
            }
        },
        {
            title: 'Recompute Absolute diagnostics',
            kind: 'quickfix',
            command: {
                title: 'Recompute diagnostics',
                command: 'absolute.languageServer.refreshDiagnostics',
                arguments: [uri]
            }
        }
    ];
}

function handleRequest(message) {
    const { id, method, params } = message;
    try {
        switch (method) {
        case 'initialize': {
            workspaceFolders = params.workspaceFolders || [];
            if ((!workspaceFolders || !workspaceFolders.length) && params.rootUri) {
                workspaceFolders = [{ uri: params.rootUri }];
            }
            if (params.initializationOptions) {
                if (params.initializationOptions.compilerPath)
                    compilerPath = params.initializationOptions.compilerPath;
                if (Array.isArray(params.initializationOptions.compilerArguments))
                    compilerArguments = params.initializationOptions.compilerArguments;
                if (Array.isArray(params.initializationOptions.plugins))
                    pluginIndex = lang.loadEditorIndex(params.initializationOptions.plugins,
                        params.initializationOptions.editorMetadata || []);
            }
            refreshWorkspaceIndex();
            respond(id, {
                capabilities: {
                    textDocumentSync: { openClose: true, change: 1, save: { includeText: false } },
                    completionProvider: { triggerCharacters: ['.'] },
                    hoverProvider: true,
                    definitionProvider: true,
                    referencesProvider: true,
                    renameProvider: { prepareProvider: true },
                    documentSymbolProvider: true,
                    workspaceSymbolProvider: true,
                    documentFormattingProvider: true,
                    codeActionProvider: { codeActionKinds: ['quickfix', 'source.formatDocument'] },
                    semanticTokensProvider: {
                        legend: {
                            tokenTypes: ['keyword', 'type', 'function', 'namespace'],
                            tokenModifiers: []
                        },
                        full: true
                    }
                },
                serverInfo: { name: 'absolute-lsp', version: '0.3.0' }
            });
            return;
        }
        case 'shutdown':
            respond(id, null);
            return;
        case 'textDocument/completion': {
            const doc = documents.get(params.textDocument.uri);
            respond(id, doc ? lang.completionItems(pluginIndex, doc.text, params.position) : []);
            return;
        }
        case 'textDocument/hover': {
            const doc = documents.get(params.textDocument.uri);
            respond(id, doc ? (lang.hoverFor(pluginIndex, doc.text, params.position) || null) : null);
            return;
        }
        case 'textDocument/definition':
            respond(id, definition(params.textDocument.uri, params.position));
            return;
        case 'textDocument/references':
            respond(id, references(params.textDocument.uri, params.position,
                !(params.context && params.context.includeDeclaration === false)));
            return;
        case 'textDocument/prepareRename': {
            const doc = documents.get(params.textDocument.uri);
            if (!doc) { respond(id, null); return; }
            const word = lang.wordAt(doc.text, params.position);
            respond(id, word ? { range: word.range, placeholder: word.name } : null);
            return;
        }
        case 'textDocument/rename':
            respond(id, rename(params.textDocument.uri, params.position, params.newName));
            return;
        case 'textDocument/documentSymbol':
            respond(id, documentSymbols(params.textDocument.uri));
            return;
        case 'workspace/symbol': {
            const query = (params.query || '').toLowerCase();
            const items = [];
            for (const [name, symbols] of workspaceSymbols.entries()) {
                if (query && !name.toLowerCase().includes(query)) continue;
                for (const symbol of symbols) {
                    items.push({
                        name: symbol.name,
                        kind: symbol.kind,
                        location: { uri: symbol.uri, range: symbol.selectionRange },
                        containerName: symbol.detail
                    });
                }
            }
            respond(id, items.slice(0, 200));
            return;
        }
        case 'textDocument/formatting':
            respond(id, formatting(params.textDocument.uri, params.options));
            return;
        case 'textDocument/codeAction':
            respond(id, codeActions(params.textDocument.uri, params.range));
            return;
        case 'textDocument/semanticTokens/full': {
            const doc = documents.get(params.textDocument.uri);
            respond(id, doc ? lang.semanticTokens(pluginIndex, doc.text) : { data: [] });
            return;
        }
        case 'absolute/setConfiguration': {
            if (params.compilerPath) compilerPath = params.compilerPath;
            if (Array.isArray(params.compilerArguments)) compilerArguments = params.compilerArguments;
            if (Array.isArray(params.plugins) || Array.isArray(params.editorMetadata)) {
                pluginIndex = lang.loadEditorIndex(params.plugins || [], params.editorMetadata || []);
            }
            refreshWorkspaceIndex();
            respond(id, { ok: true, plugins: pluginIndex.plugins.length, symbols: workspaceSymbols.size });
            return;
        }
        case 'absolute/formatText': {
            respond(id, { text: lang.formatDocument(params.text || '', params.options || {}) });
            return;
        }
        case 'absolute/opaqueSourceMaps': {
            const uri = params && params.textDocument && params.textDocument.uri;
            const tags = (params && params.tags) || ['shader'];
            let text = documentText(uri);
            if (text === undefined && uri) {
                respond(id, { blocks: [] });
                return;
            }
            const blocks = lang.extractOpaqueBlocks(uri, text || '', tags).map(block => ({
                tag: block.tag,
                name: block.name,
                hostUri: block.hostUri,
                virtualUri: block.virtualUri,
                hostRange: block.hostRange,
                hostBreakpointLine: block.hostBreakpointLine,
                language: block.language,
                body: block.body,
                mappings: block.mappings
            }));
            respond(id, { blocks });
            return;
        }
        case 'absolute/evaluate': {
            const source = lang.evaluateExpressionSource(
                params && params.expression,
                (params && params.preamble) || '');
            respond(id, { source });
            return;
        }
        default:
            if (id !== undefined) respondError(id, -32601, `Method not found: ${method}`);
        }
    } catch (error) {
        if (id !== undefined) respondError(id, -32000, error.message || String(error));
        else log(error.stack || error.message || error);
    }
}

function handleNotification(message) {
    const { method, params } = message;
    switch (method) {
    case 'initialized':
        initialized = true;
        log('Absolute LSP initialized');
        break;
    case 'exit':
        process.exit(0);
        break;
    case 'textDocument/didOpen': {
        const doc = params.textDocument;
        documents.set(doc.uri, { text: doc.text, version: doc.version });
        publishDiagnostics(doc.uri);
        break;
    }
    case 'textDocument/didChange': {
        const uri = params.textDocument.uri;
        const changes = params.contentChanges || [];
        if (!changes.length) break;
        // Full document sync only.
        const text = changes[changes.length - 1].text;
        documents.set(uri, { text, version: params.textDocument.version });
        break;
    }
    case 'textDocument/didSave':
        refreshWorkspaceIndex();
        publishDiagnostics(params.textDocument.uri);
        break;
    case 'textDocument/didClose':
        documents.delete(params.textDocument.uri);
        notify('textDocument/publishDiagnostics', { uri: params.textDocument.uri, diagnostics: [] });
        break;
    case 'workspace/didChangeWorkspaceFolders':
        if (params.event) {
            const removed = new Set((params.event.removed || []).map(f => f.uri));
            workspaceFolders = (workspaceFolders || []).filter(f => !removed.has(f.uri));
            workspaceFolders = workspaceFolders.concat(params.event.added || []);
            refreshWorkspaceIndex();
        }
        break;
    case 'workspace/didChangeConfiguration':
        if (params.settings && params.settings.absolute) {
            const absolute = params.settings.absolute;
            if (absolute.compilerPath) compilerPath = absolute.compilerPath;
            if (Array.isArray(absolute.compilerArguments)) compilerArguments = absolute.compilerArguments;
        }
        break;
    default:
        break;
    }
}

function dispatch(message) {
    if (!message || typeof message !== 'object') return;
    if (Object.prototype.hasOwnProperty.call(message, 'id') && message.method)
        handleRequest(message);
    else if (message.method)
        handleNotification(message);
}

let buffer = Buffer.alloc(0);
process.stdin.on('data', chunk => {
    buffer = Buffer.concat([buffer, chunk]);
    while (true) {
        const headerEnd = buffer.indexOf('\r\n\r\n');
        if (headerEnd < 0) return;
        const header = buffer.slice(0, headerEnd).toString('utf8');
        const match = header.match(/Content-Length:\s*(\d+)/i);
        if (!match) {
            buffer = buffer.slice(headerEnd + 4);
            continue;
        }
        const length = parseInt(match[1], 10);
        const total = headerEnd + 4 + length;
        if (buffer.length < total) return;
        const body = buffer.slice(headerEnd + 4, total).toString('utf8');
        buffer = buffer.slice(total);
        try {
            dispatch(JSON.parse(body));
        } catch (error) {
            log(`Failed to parse LSP message: ${error.message || error}`);
        }
    }
});

process.stdin.on('end', () => process.exit(0));
process.stdin.resume();
