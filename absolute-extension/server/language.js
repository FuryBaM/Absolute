'use strict';

const fs = require('fs');
const path = require('path');
const childProcess = require('child_process');

const CORE_KEYWORDS = [
    'if', 'else', 'switch', 'case', 'default', 'for', 'while', 'foreach', 'in', 'do',
    'break', 'continue', 'new', 'delete', 'namespace', 'import', 'return', 'true', 'false',
    'null', 'class', 'struct', 'enum', 'group', 'this', 'public', 'private', 'protected',
    'sealed', 'internal', 'virtual', 'override', 'const', 'static', 'auto', 'async', 'await',
    'catch', 'finally', 'try', 'throw', 'yield', 'get', 'set', 'operator', 'extension',
    'extern', 'export', 'raw', 'weak', 'ref', 'defer', 'match', 'fn', 'func', 'interface',
    'base', 'is', 'as', 'move', 'copy', 'spawn', 'task'
];

const CORE_TYPES = [
    'int8', 'int16', 'int32', 'int64', 'uint8', 'uint16', 'uint32', 'uint64',
    'float', 'double', 'char', 'bool', 'string', 'void', 'dynamic'
];

function emptyPluginIndex() {
    return {
        plugins: [],
        entries: [],
        hover: new Map(),
        keywordNames: new Set(CORE_KEYWORDS),
        typeNames: new Set(CORE_TYPES),
        functionNames: new Set(),
        namespaceNames: new Set(),
        errors: []
    };
}

function stripComments(text) {
    let out = '';
    let i = 0;
    let block = false;
    while (i < text.length) {
        if (block) {
            if (text.startsWith('*/', i)) { block = false; out += '  '; i += 2; }
            else { out += text[i] === '\n' ? '\n' : ' '; i += 1; }
            continue;
        }
        if (text.startsWith('//', i)) {
            while (i < text.length && text[i] !== '\n') { out += ' '; i += 1; }
            continue;
        }
        if (text.startsWith('/*', i)) { block = true; out += '  '; i += 2; continue; }
        if (text[i] === '"' || text[i] === "'") {
            const q = text[i];
            out += q; i += 1;
            while (i < text.length) {
                out += text[i];
                if (text[i] === '\\' && i + 1 < text.length) { out += text[i + 1]; i += 2; continue; }
                if (text[i] === q) { i += 1; break; }
                i += 1;
            }
            continue;
        }
        out += text[i];
        i += 1;
    }
    return out;
}

function lineOffsets(text) {
    const offsets = [0];
    for (let i = 0; i < text.length; ++i) if (text[i] === '\n') offsets.push(i + 1);
    return offsets;
}

function offsetToPosition(offsets, offset) {
    let low = 0;
    let high = offsets.length - 1;
    while (low <= high) {
        const mid = (low + high) >> 1;
        if (offsets[mid] <= offset) low = mid + 1;
        else high = mid - 1;
    }
    const line = Math.max(0, high);
    return { line, character: offset - offsets[line] };
}

function positionToOffset(offsets, position, textLength) {
    if (position.line < 0) return 0;
    if (position.line >= offsets.length) return textLength;
    return Math.min(textLength, offsets[position.line] + Math.max(0, position.character));
}

function rangeAt(offsets, start, end) {
    return { start: offsetToPosition(offsets, start), end: offsetToPosition(offsets, end) };
}

function extractSymbols(uri, text) {
    const clean = stripComments(text);
    const offsets = lineOffsets(text);
    const symbols = [];
    const patterns = [
        { kind: 5, re: /\b(?:public|private|protected|sealed|static|virtual|override|async|const)*\s*(?:class|struct|interface|enum)\s+([A-Za-z_][A-Za-z0-9_]*)/g, symbolKind: 'type' },
        { kind: 3, re: /\bnamespace\s+([A-Za-z_][A-Za-z0-9_.]*)/g, symbolKind: 'namespace' },
        { kind: 12, re: /\b(?:public|private|protected|static|virtual|override|async|const|sealed)*\s*(?:func(?:<[^>]*>)?\s+)?(?:[A-Za-z_][A-Za-z0-9_.*<>\[\]]+\s+)+([A-Za-z_][A-Za-z0-9_]*)\s*\(/g, symbolKind: 'function' },
        { kind: 12, re: /\basync\s+(?:[A-Za-z_][A-Za-z0-9_.*<>\[\]]+\s+)+([A-Za-z_][A-Za-z0-9_]*)\s*\(/g, symbolKind: 'function' }
    ];

    const seen = new Set();
    for (const pattern of patterns) {
        pattern.re.lastIndex = 0;
        let match;
        while ((match = pattern.re.exec(clean)) !== null) {
            const name = match[1];
            if (!name || CORE_KEYWORDS.includes(name) || CORE_TYPES.includes(name)) continue;
            const start = match.index + match[0].lastIndexOf(name);
            const end = start + name.length;
            const key = `${name}@${start}`;
            if (seen.has(key)) continue;
            seen.add(key);
            symbols.push({
                name,
                kind: pattern.kind,
                symbolKind: pattern.symbolKind,
                uri,
                range: rangeAt(offsets, start, end),
                selectionRange: rangeAt(offsets, start, end),
                detail: pattern.symbolKind
            });
        }
    }
    return symbols;
}

function wordAt(text, position) {
    const offsets = lineOffsets(text);
    const offset = positionToOffset(offsets, position, text.length);
    let start = offset;
    let end = offset;
    while (start > 0 && /[A-Za-z0-9_.]/.test(text[start - 1])) start -= 1;
    while (end < text.length && /[A-Za-z0-9_.]/.test(text[end])) end += 1;
    if (start === end) return undefined;
    const full = text.slice(start, end);
    // Prefer the rightmost identifier segment under cursor.
    const parts = full.split('.');
    let segmentStart = start;
    let remaining = offset - start;
    let name = parts[0];
    for (const part of parts) {
        if (remaining <= part.length) {
            name = part;
            break;
        }
        remaining -= part.length + 1;
        segmentStart += part.length + 1;
        name = part;
    }
    return {
        name,
        full,
        range: rangeAt(offsets, segmentStart, segmentStart + name.length),
        fullRange: rangeAt(offsets, start, end)
    };
}

function findReferencesInText(uri, text, name) {
    const clean = stripComments(text);
    const offsets = lineOffsets(text);
    const results = [];
    const re = new RegExp(`\\b${name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}\\b`, 'g');
    let match;
    while ((match = re.exec(clean)) !== null) {
        results.push({
            uri,
            range: rangeAt(offsets, match.index, match.index + name.length)
        });
    }
    return results;
}

function formatDocument(text, options = {}) {
    const tabSize = options.tabSize || 4;
    const insertSpaces = options.insertSpaces !== false;
    const indentUnit = insertSpaces ? ' '.repeat(tabSize) : '\t';
    const lines = text.replace(/\r\n/g, '\n').replace(/\r/g, '\n').split('\n');
    let depth = 0;
    let inBlockComment = false;
    const out = [];

    for (let raw of lines) {
        let line = raw.replace(/[ \t]+$/g, '');
        const trimmed = line.trim();

        if (!inBlockComment) {
            const closes = (trimmed.match(/^[\}\]\)]+/) || [''])[0].length;
            if (closes) depth = Math.max(0, depth - closes);
        }

        if (trimmed.length === 0) {
            out.push('');
        } else if (inBlockComment) {
            out.push(line);
        } else {
            out.push(indentUnit.repeat(depth) + trimmed);
        }

        // Track block comments and braces outside strings (approximate).
        let i = 0;
        let inString = false;
        let quote = '';
        while (i < line.length) {
            const ch = line[i];
            if (inBlockComment) {
                if (line.startsWith('*/', i)) { inBlockComment = false; i += 2; continue; }
                i += 1; continue;
            }
            if (!inString && line.startsWith('//', i)) break;
            if (!inString && line.startsWith('/*', i)) { inBlockComment = true; i += 2; continue; }
            if (inString) {
                if (ch === '\\') { i += 2; continue; }
                if (ch === quote) inString = false;
                i += 1; continue;
            }
            if (ch === '"' || ch === "'") { inString = true; quote = ch; i += 1; continue; }
            if (ch === '{' || ch === '(' || ch === '[') depth += 1;
            if (ch === '}' || ch === ')' || ch === ']') depth = Math.max(0, depth - 1);
            i += 1;
        }
    }

    let formatted = out.join('\n');
    if (!formatted.endsWith('\n')) formatted += '\n';
    return formatted;
}

function parseCompilerDiagnostics(stderrText, uri) {
    const diagnostics = [];
    const lines = String(stderrText || '').split(/\r?\n/);
    for (const line of lines) {
        if (!line.trim()) continue;
        let severity = 1; // Error
        if (/warning/i.test(line)) severity = 2;
        const loc = line.match(/at line\s+(\d+)(?:,\s*column\s+(\d+))?/i);
        const message = line
            .replace(/^Semantic error:\s*/i, '')
            .replace(/^Syntax Error:\s*/i, '')
            .replace(/^Lexical error:\s*/i, '')
            .replace(/^Error:\s*/i, '')
            .trim() || line;
        let startLine = 0;
        let startChar = 0;
        if (loc) {
            startLine = Math.max(0, parseInt(loc[1], 10) - 1);
            startChar = Math.max(0, (parseInt(loc[2] || '1', 10) || 1) - 1);
        } else if (!/semantic error|syntax error|lexical error|error:/i.test(line)) {
            continue;
        }
        diagnostics.push({
            range: {
                start: { line: startLine, character: startChar },
                end: { line: startLine, character: startChar + 1 }
            },
            severity,
            source: 'absolutec',
            message
        });
    }
    return diagnostics;
}

function runCompilerDiagnostics(filePath, compilerPath, extraArgs = []) {
    if (!compilerPath || !filePath || !fs.existsSync(filePath)) return [];
    try {
        const result = childProcess.spawnSync(
            compilerPath,
            [filePath, ...extraArgs],
            { encoding: 'utf8', timeout: 20000, windowsHide: true }
        );
        const text = `${result.stdout || ''}\n${result.stderr || ''}`;
        return parseCompilerDiagnostics(text, filePath);
    } catch (error) {
        return [{
            range: { start: { line: 0, character: 0 }, end: { line: 0, character: 1 } },
            severity: 2,
            source: 'absolutec',
            message: `compiler diagnostics unavailable: ${error.message || error}`
        }];
    }
}

function walkAbsFiles(root, acc = []) {
    if (!root || !fs.existsSync(root)) return acc;
    let entries = [];
    try { entries = fs.readdirSync(root, { withFileTypes: true }); }
    catch (_) { return acc; }
    for (const entry of entries) {
        if (entry.name === '.git' || entry.name === 'node_modules' || entry.name === 'build' ||
            entry.name === '.absolute' || entry.name === 'x64' || entry.name.startsWith('.desktop'))
            continue;
        const full = path.join(root, entry.name);
        if (entry.isDirectory()) walkAbsFiles(full, acc);
        else if (entry.isFile() && entry.name.endsWith('.abs')) acc.push(full);
    }
    return acc;
}

function buildWorkspaceSymbolIndex(roots) {
    const byName = new Map();
    for (const root of roots) {
        for (const file of walkAbsFiles(root)) {
            let text = '';
            try { text = fs.readFileSync(file, 'utf8'); }
            catch (_) { continue; }
            const uri = pathToUri(file);
            for (const symbol of extractSymbols(uri, text)) {
                const list = byName.get(symbol.name) || [];
                list.push(symbol);
                byName.set(symbol.name, list);
            }
        }
    }
    return byName;
}

function pathToUri(filePath) {
    let resolved = path.resolve(filePath).replace(/\\/g, '/');
    if (!resolved.startsWith('/')) resolved = `/${resolved}`;
    return encodeURI(`file://${resolved}`);
}

function uriToPath(uri) {
    if (!uri) return '';
    let value = uri;
    if (value.startsWith('file://')) {
        value = decodeURIComponent(value.slice('file://'.length));
        if (process.platform === 'win32' && /^\/[A-Za-z]:/.test(value)) value = value.slice(1);
    }
    return value.replace(/\//g, path.sep);
}

function generateDocMarkdown(roots) {
    const index = buildWorkspaceSymbolIndex(roots);
    const names = [...index.keys()].sort((a, b) => a.localeCompare(b));
    let md = '# Absolute API (generated)\n\n';
    md += `Generated from ${roots.join(', ')}\n\n`;
    for (const name of names) {
        const symbols = index.get(name) || [];
        md += `## ${name}\n\n`;
        for (const symbol of symbols) {
            const file = uriToPath(symbol.uri);
            md += `- **${symbol.symbolKind}** \`${name}\` - \`${file}:${symbol.range.start.line + 1}\`\n`;
        }
        md += '\n';
    }
    return md;
}

function metadataItem(raw, kind, plugin, owner) {
    const item = typeof raw === 'string' ? { name: raw } : raw;
    if (!item || typeof item.name !== 'string' || !item.name) return undefined;
    const fullName = owner ? `${owner}.${item.name}` : item.name;
    return {
        kind,
        name: item.name,
        fullName,
        detail: item.detail || `${kind} ${fullName}`,
        documentation: item.documentation || '',
        snippet: item.snippet,
        plugin
    };
}

function addMetadata(index, metadata, pluginName) {
    if (!metadata || (metadata.schemaVersion !== undefined && metadata.schemaVersion !== 1)) {
        throw new Error(`unsupported editor metadata schema ${metadata && metadata.schemaVersion}`);
    }
    const groups = [
        ['keywords', 'keyword'], ['namespaces', 'namespace'], ['types', 'type'], ['functions', 'function']
    ];
    for (const [property, kind] of groups) {
        for (const raw of Array.isArray(metadata[property]) ? metadata[property] : []) {
            const entry = metadataItem(raw, kind, pluginName);
            if (!entry) continue;
            index.entries.push(entry);
            index.hover.set(entry.fullName, entry);
            index.hover.set(entry.name.split('.').pop(), entry);
            const names = kind === 'keyword' ? index.keywordNames : kind === 'type' ? index.typeNames :
                kind === 'function' ? index.functionNames : index.namespaceNames;
            names.add(entry.fullName);
            names.add(entry.fullName.split('.').pop());
            if (kind === 'type' && raw && Array.isArray(raw.members)) {
                for (const member of raw.members) {
                    const memberEntry = metadataItem(member, 'member', pluginName, entry.fullName);
                    if (!memberEntry) continue;
                    index.entries.push(memberEntry);
                    index.hover.set(memberEntry.fullName, memberEntry);
                    if (!index.hover.has(memberEntry.name)) index.hover.set(memberEntry.name, memberEntry);
                    index.functionNames.add(memberEntry.name);
                }
            }
        }
    }
    for (const snippet of Array.isArray(metadata.snippets) ? metadata.snippets : []) {
        if (!snippet || !snippet.label || !snippet.body) continue;
        index.entries.push({
            kind: 'snippet', name: snippet.label, fullName: snippet.label,
            detail: snippet.detail || 'Absolute plugin snippet', documentation: snippet.documentation || '',
            snippet: snippet.body, plugin: pluginName
        });
    }
}

function readJson(file) {
    const text = fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, '');
    return JSON.parse(text);
}

function loadPluginMetadata(index, manifestFile) {
    const absolute = path.resolve(manifestFile);
    if (!fs.existsSync(absolute)) return;
    const manifest = readJson(absolute);
    let metadata = manifest.editor;
    if (typeof metadata === 'string') {
        metadata = readJson(path.resolve(path.dirname(absolute), metadata));
    } else if (!metadata) {
        const inferred = absolute.slice(0, -'.absplugin'.length) + '.editor.json';
        if (fs.existsSync(inferred)) metadata = readJson(inferred);
    }
    if (metadata) addMetadata(index, metadata, manifest.name || path.basename(absolute));
    index.plugins.push({
        name: manifest.name || path.basename(absolute),
        version: manifest.version || '?',
        abi: manifest.abi,
        manifest: absolute
    });
}

function loadEditorIndex(pluginFiles = [], editorMetadataFiles = []) {
    const index = emptyPluginIndex();
    for (const file of pluginFiles) {
        try {
            if (file.endsWith('.absplugin')) loadPluginMetadata(index, file);
            else if (file.endsWith('.editor.json')) {
                const metadata = readJson(file);
                addMetadata(index, metadata, metadata.plugin || path.basename(file, '.editor.json'));
            }
        } catch (error) {
            index.errors.push(`${file}: ${error.message || error}`);
        }
    }
    for (const file of editorMetadataFiles) {
        try {
            const metadata = readJson(file);
            addMetadata(index, metadata, metadata.plugin || path.basename(file, '.editor.json'));
        } catch (error) {
            index.errors.push(`${file}: ${error.message || error}`);
        }
    }
    return index;
}

function completionItems(index, text, position) {
    const offsets = lineOffsets(text);
    const offset = positionToOffset(offsets, position, text.length);
    const before = text.slice(Math.max(0, offsets[position.line] || 0), offset);
    const qualified = before.match(/([A-Za-z_][A-Za-z0-9_.]*)\.$/);
    const qualifier = qualified ? qualified[1] : undefined;
    const items = [];

    if (!qualifier) {
        for (const keyword of CORE_KEYWORDS) {
            items.push({ label: keyword, kind: 14, detail: 'keyword' });
        }
        for (const type of CORE_TYPES) {
            items.push({ label: type, kind: 25, detail: 'type' });
        }
    }

    for (const entry of index.entries) {
        if (qualifier && !(entry.fullName.startsWith(`${qualifier}.`) || entry.kind === 'member')) continue;
        let label = entry.fullName;
        let insertText = entry.snippet || entry.fullName;
        if (qualifier && entry.fullName.startsWith(`${qualifier}.`)) {
            label = entry.fullName.slice(qualifier.length + 1);
            if (typeof insertText === 'string' && insertText.startsWith(`${qualifier}.`))
                insertText = insertText.slice(qualifier.length + 1);
        } else if (qualifier && entry.kind === 'member') {
            label = entry.name;
            insertText = entry.snippet || entry.name;
        }
        const kind = entry.kind === 'keyword' ? 14 :
            entry.kind === 'type' ? 7 :
            entry.kind === 'namespace' ? 9 :
            entry.kind === 'snippet' ? 15 :
            entry.kind === 'member' ? 2 : 3;
        items.push({
            label,
            kind,
            detail: `${entry.detail} — ${entry.plugin}`,
            documentation: entry.documentation || '',
            insertText,
            insertTextFormat: entry.snippet ? 2 : 1
        });
    }
    return items;
}

function hoverFor(index, text, position) {
    const word = wordAt(text, position);
    if (!word) return undefined;
    const entry = index.hover.get(word.full) || index.hover.get(word.name) ||
        index.hover.get(word.name.split('.').pop());
    if (!entry) return undefined;
    const value = {
        contents: {
            kind: 'markdown',
            value: '```absolute\n' + entry.detail + '\n```\n\n' +
                (entry.documentation ? entry.documentation + '\n\n' : '') +
                `_Plugin: ${entry.plugin}_`
        },
        range: word.range
    };
    return value;
}

function semanticTokens(index, text) {
    // legend: keyword=0 type=1 function=2 namespace=3
    const data = [];
    const lines = text.split(/\r?\n/);
    let prevLine = 0;
    let prevStart = 0;
    let blockComment = false;
    for (let lineNumber = 0; lineNumber < lines.length; ++lineNumber) {
        const line = lines[lineNumber];
        let offset = 0;
        while (offset < line.length) {
            if (blockComment) {
                const end = line.indexOf('*/', offset);
                if (end < 0) break;
                blockComment = false;
                offset = end + 2;
                continue;
            }
            if (line.startsWith('//', offset)) break;
            if (line.startsWith('/*', offset)) { blockComment = true; offset += 2; continue; }
            if (line[offset] === '"' || line[offset] === "'") {
                const quote = line[offset++];
                while (offset < line.length) {
                    if (line[offset] === '\\') offset += 2;
                    else if (line[offset++] === quote) break;
                }
                continue;
            }
            const match = /^[A-Za-z_][A-Za-z0-9_]*/.exec(line.slice(offset));
            if (!match) { offset += 1; continue; }
            const word = match[0];
            let tokenType;
            if (index.keywordNames.has(word) || CORE_KEYWORDS.includes(word)) tokenType = 0;
            else if (index.typeNames.has(word) || CORE_TYPES.includes(word)) tokenType = 1;
            else if (index.functionNames.has(word)) tokenType = 2;
            else if (index.namespaceNames.has(word)) tokenType = 3;
            if (tokenType !== undefined) {
                const deltaLine = lineNumber - prevLine;
                const deltaStart = deltaLine === 0 ? offset - prevStart : offset;
                data.push(deltaLine, deltaStart, word.length, tokenType, 0);
                prevLine = lineNumber;
                prevStart = offset;
            }
            offset += word.length;
        }
    }
    return { data };
}

module.exports = {
    CORE_KEYWORDS,
    CORE_TYPES,
    emptyPluginIndex,
    extractSymbols,
    wordAt,
    findReferencesInText,
    formatDocument,
    parseCompilerDiagnostics,
    runCompilerDiagnostics,
    walkAbsFiles,
    buildWorkspaceSymbolIndex,
    pathToUri,
    uriToPath,
    generateDocMarkdown,
    loadEditorIndex,
    completionItems,
    hoverFor,
    semanticTokens,
    lineOffsets,
    offsetToPosition,
    positionToOffset
};
