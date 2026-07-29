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
    'base', 'is', 'as', 'move', 'copy', 'seal', 'unseal', 'spawn', 'task'
];

const CORE_TYPES = [
    'int8', 'int16', 'int32', 'int64', 'uint8', 'uint16', 'uint32', 'uint64',
    'float', 'double', 'char', 'bool', 'string', 'void', 'dynamic'
];

const CORE_HOVER = {
    auto: ['inferred local type', 'The analyzer infers the static type from the initializer. Ownership is preserved.'],
    new: ['managed allocation', 'Creates a checked managed owner. The owner is destroyed automatically when its scope ends.'],
    delete: ['explicit owner destruction', 'Destroys an owner before normal scope exit. Usually unnecessary for RAII values.'],
    raw: ['raw pointer qualifier', 'Unsafe address without managed lifetime checks or automatic pointee destruction.'],
    weak: ['weak managed reference', 'Non-owning observer that expires when the strong owner is destroyed.'],
    move: ['ownership transfer', 'Transfers an owning value and leaves the source moved-from.'],
    copy: ['explicit owning copy', 'Creates a distinct owning copy when the value supports copying.'],
    seal: [
        'raw void* seal<T>(T* movedOwner)',
        'Consumes `move(owner)`, expires sender aliases, and creates an opaque one-shot transfer capsule.'
    ],
    unseal: [
        'T* unseal<T>(raw void* capsule)',
        'Consumes a transfer capsule and establishes the receiver as the new managed owner.'
    ],
    defer: ['deferred action', 'Runs at scope exit, including return, break, continue, and exception cleanup.'],
    namespace: ['namespace declaration', 'Groups related types and functions under a qualified name.'],
    extern: ['native declaration', 'Declares an externally implemented ABI function. Plugin users normally receive these through a prelude.'],
    string: ['UTF-8 string', 'Built-in UTF-8 string view/value used by Absolute APIs.'],
    auto_cleanup: ['RAII cleanup', 'Managed owners and classes with destroy() are released automatically at scope exit.']
};

function emptyPluginIndex() {
    return {
        plugins: [],
        entries: [],
        entryByKey: new Map(),
        hover: new Map(),
        membersByOwner: new Map(),
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

function diagnosticTokenEnd(sourceLines, line, startChar) {
    const text = sourceLines && sourceLines[line];
    if (!text || startChar >= text.length) return startChar + 1;
    const tail = text.slice(startChar);
    const word = tail.match(/^[A-Za-z_][A-Za-z0-9_]*/);
    if (word) return startChar + word[0].length;
    const number = tail.match(/^(?:\d+(?:\.\d*)?|\.\d+)/);
    if (number) return startChar + number[0].length;
    return startChar + 1;
}

function parseCompilerDiagnostics(stderrText, uri, sourceText) {
    const diagnostics = [];
    const lines = String(stderrText || '').split(/\r?\n/);
    const sourceLines = typeof sourceText === 'string'
        ? sourceText.split(/\r?\n/) : null;
    for (const line of lines) {
        if (!line.trim()) continue;
        let severity = 1; // Error
        if (/warning/i.test(line)) severity = 2;
        const structured = line.match(
            /^(.*):(\d+):(\d+):\s*(Semantic error|Syntax Error|Lexical error|Error):\s*(.*)$/i);
        const legacyLocation = line.match(
            /at line\s+(\d+)(?:,\s*column\s+(\d+))?/i);
        let message = (structured ? structured[5] : line)
            .replace(/^Semantic error:\s*/i, '')
            .replace(/^Syntax Error:\s*/i, '')
            .replace(/^Lexical error:\s*/i, '')
            .replace(/^Error:\s*/i, '')
            .replace(/\s+at line\s+\d+(?:,\s*column\s+\d+)?\s*$/i, '')
            .trim() || line;
        let startLine = 0;
        let startChar = 0;
        if (structured) {
            const diagnosticPath = path.resolve(structured[1]);
            if (uri && path.resolve(uri).toLowerCase() !== diagnosticPath.toLowerCase())
                continue;
            startLine = Math.max(0, parseInt(structured[2], 10) - 1);
            startChar = Math.max(0, parseInt(structured[3], 10) - 1);
        } else if (legacyLocation) {
            startLine = Math.max(0, parseInt(legacyLocation[1], 10) - 1);
            startChar = Math.max(
                0, (parseInt(legacyLocation[2] || '1', 10) || 1) - 1);
        } else if (!/semantic error|syntax error|lexical error|error:/i.test(line)) {
            continue;
        }
        diagnostics.push({
            range: {
                start: { line: startLine, character: startChar },
                end: {
                    line: startLine,
                    character: diagnosticTokenEnd(sourceLines, startLine, startChar)
                }
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
            {
                encoding: 'utf8',
                timeout: 20000,
                windowsHide: true,
                cwd: path.dirname(filePath)
            }
        );
        if (result.error) throw result.error;
        const text = `${result.stdout || ''}\n${result.stderr || ''}`;
        const sourceText = fs.readFileSync(filePath, 'utf8');
        return parseCompilerDiagnostics(text, filePath, sourceText);
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
        plugin,
        owner,
        ownership: item.ownership || '',
        since: item.since || ''
    };
}

function upsertIndexEntry(index, entry) {
    const key = `${entry.kind}:${entry.fullName}`;
    const existing = index.entryByKey.get(key);
    if (existing) {
        Object.assign(existing, entry);
        return existing;
    }
    index.entries.push(entry);
    index.entryByKey.set(key, entry);
    return entry;
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
            const parsedEntry = metadataItem(raw, kind, pluginName);
            if (!parsedEntry) continue;
            const entry = upsertIndexEntry(index, parsedEntry);
            index.hover.set(entry.fullName, entry);
            index.hover.set(entry.name.split('.').pop(), entry);
            const names = kind === 'keyword' ? index.keywordNames : kind === 'type' ? index.typeNames :
                kind === 'function' ? index.functionNames : index.namespaceNames;
            names.add(entry.fullName);
            names.add(entry.fullName.split('.').pop());
            if (kind === 'type' && raw && Array.isArray(raw.members)) {
                const ownerMembers = index.membersByOwner.get(entry.fullName) || [];
                for (const member of raw.members) {
                    const memberEntry = metadataItem(member, 'member', pluginName, entry.fullName);
                    if (!memberEntry) continue;
                    const storedMember = upsertIndexEntry(index, memberEntry);
                    if (!ownerMembers.some(item => item.fullName === storedMember.fullName))
                        ownerMembers.push(storedMember);
                    index.hover.set(storedMember.fullName, storedMember);
                    if (!index.hover.has(storedMember.name)) index.hover.set(storedMember.name, storedMember);
                    index.functionNames.add(storedMember.name);
                }
                index.membersByOwner.set(entry.fullName, ownerMembers);
            }
        }
    }
    for (const snippet of Array.isArray(metadata.snippets) ? metadata.snippets : []) {
        if (!snippet || !snippet.label || !snippet.body) continue;
        upsertIndexEntry(index, {
            kind: 'snippet', name: snippet.label, fullName: snippet.label,
            detail: snippet.detail || 'Absolute plugin snippet', documentation: snippet.documentation || '',
            snippet: snippet.body, plugin: pluginName
        });
    }
}

function addPreludeMetadata(index, preludeFile, pluginName) {
    const absolute = path.resolve(preludeFile);
    if (path.extname(absolute).toLowerCase() !== '.abs') {
        throw new Error(`plugin prelude must use the .abs extension: ${absolute}`);
    }
    if (!fs.existsSync(absolute) || !fs.statSync(absolute).isFile()) {
        throw new Error(`plugin prelude does not exist: ${absolute}`);
    }
    const source = fs.readFileSync(absolute, 'utf8').replace(/^\uFEFF/, '');
    const symbols = extractSymbols(pathToUri(absolute), source);
    const lines = source.replace(/\r\n/g, '\n').split('\n');
    for (const symbol of symbols) {
        const kind = symbol.symbolKind === 'type'
            ? 'type'
            : symbol.symbolKind === 'namespace'
                ? 'namespace'
                : 'function';
        const declaration = (lines[symbol.range.start.line] || '').trim();
        const entry = upsertIndexEntry(index, metadataItem({
            name: symbol.name,
            detail: declaration || `${kind} ${symbol.name}`,
            documentation: `Declared by ${pluginName} in ${path.basename(absolute)}.`
        }, kind, pluginName));
        index.hover.set(entry.fullName, entry);
        index.hover.set(entry.name, entry);
        const names = kind === 'type'
            ? index.typeNames
            : kind === 'namespace'
                ? index.namespaceNames
                : index.functionNames;
        names.add(entry.fullName);
        names.add(entry.name);
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
    const configuredPreludes = [];
    if (typeof manifest.prelude === 'string' && manifest.prelude) {
        configuredPreludes.push(manifest.prelude);
    }
    if (Array.isArray(manifest.preludes)) {
        configuredPreludes.push(...manifest.preludes.filter(item => typeof item === 'string' && item));
    }
    for (const prelude of [...new Set(configuredPreludes)]) {
        addPreludeMetadata(
            index,
            path.resolve(path.dirname(absolute), prelude),
            manifest.name || path.basename(absolute));
    }
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
    const inferredQualifier = qualifier && !qualifier.includes('.')
        ? variableInfo(index, text, position, qualifier)
        : undefined;
    const qualifierOwner = inferredQualifier && inferredQualifier.type
        ? inferredQualifier.type.replace(/^(?:raw|weak)\s+/, '').replace(/\*$/, '')
        : (qualifier && index.membersByOwner.has(qualifier) ? qualifier : undefined);
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
        if (qualifierOwner) {
            if (entry.owner !== qualifierOwner) continue;
        } else if (qualifier) {
            if (!entry.fullName.startsWith(`${qualifier}.`)) continue;
            const remainder = entry.fullName.slice(qualifier.length + 1);
            if (remainder.includes('.')) continue;
        }
        let label = entry.fullName;
        let insertText = entry.snippet || entry.fullName;
        if (qualifierOwner) {
            label = entry.name;
            insertText = entry.snippet || entry.name;
        } else if (qualifier && entry.fullName.startsWith(`${qualifier}.`)) {
            label = entry.fullName.slice(qualifier.length + 1);
            if (typeof insertText === 'string' && insertText.startsWith(`${qualifier}.`))
                insertText = insertText.slice(qualifier.length + 1);
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

function declarationSignatureAt(text, name) {
    const escaped = name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    const patterns = [
        new RegExp(`^[ \\t]*(?:public|private|protected|static|virtual|override|async|const|sealed|export(?:\\s+"[^"]+")?|extern(?:\\s+"[^"]+")?\\s+)*[^\\n;{}]*\\b${escaped}\\s*\\([^\\n{}]*\\)`, 'gm'),
        new RegExp(`^[ \\t]*(?:class|struct|interface|enum|namespace)\\s+${escaped}\\b[^\\n{]*`, 'gm')
    ];
    let best;
    for (const pattern of patterns) {
        let match;
        while ((match = pattern.exec(text)) !== null) best = match[0].trim();
    }
    return best;
}

function variableInfo(index, text, position, variableName, resolving = new Set()) {
    if (resolving.has(variableName)) return undefined;
    resolving.add(variableName);
    const offsets = lineOffsets(text);
    const limit = positionToOffset(offsets, position, text.length);
    const clean = stripComments(text).slice(0, limit);
    const escaped = variableName.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    const candidates = [];

    const managedNew = new RegExp(
        `\\bauto\\s+${escaped}\\s*=\\s*new\\s+([A-Za-z_][A-Za-z0-9_.]*(?:<[^;()]+>)?)\\s*\\(`,
        'g'
    );
    let match;
    while ((match = managedNew.exec(clean)) !== null) {
        candidates.push({
            offset: match.index,
            type: `${match[1]}*`,
            inferred: true,
            ownership: 'managed owner',
            cleanup: true
        });
    }

    const explicit = new RegExp(
        `\\b((?:raw\\s+|weak\\s+)?[A-Za-z_][A-Za-z0-9_.]*(?:<[^;=()]+>)?(?:\\[\\])?\\s*\\*?)\\s+${escaped}\\b`,
        'g'
    );
    while ((match = explicit.exec(clean)) !== null) {
        const type = match[1].replace(/\s+/g, ' ').trim();
        if (CORE_KEYWORDS.includes(type)) continue;
        candidates.push({
            offset: match.index,
            type,
            inferred: false,
            ownership: type.startsWith('raw ') ? 'raw pointer' :
                type.startsWith('weak ') ? 'weak observer' :
                type.endsWith('*') ? 'managed pointer' : 'local value',
            cleanup: !type.startsWith('raw ') && !type.startsWith('weak ')
        });
    }

    const factory = new RegExp(
        `\\bauto\\s+${escaped}\\s*=\\s*([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*)\\s*\\(`,
        'g'
    );
    while ((match = factory.exec(clean)) !== null) {
        const receiver = variableInfo(index, text, position, match[1], resolving);
        const receiverType = receiver && receiver.type
            ? receiver.type.replace(/^(?:raw|weak)\s+/, '').replace(/\*$/, '')
            : undefined;
        const member = receiverType && index.hover.get(`${receiverType}.${match[2]}`);
        const returnType = member && /^([^()]+?)\s+[A-Za-z_][A-Za-z0-9_]*\s*\(/.exec(member.detail);
        if (returnType) {
            let type = returnType[1].trim();
            const pointerSuffix = type.endsWith('*') ? '*' : '';
            const bareType = pointerSuffix ? type.slice(0, -1) : type;
            if (!bareType.includes('.') && receiverType && receiverType.includes('.') &&
                !CORE_TYPES.includes(bareType)) {
                type = `${receiverType.slice(0, receiverType.lastIndexOf('.') + 1)}${bareType}${pointerSuffix}`;
            }
            candidates.push({
                offset: match.index,
                type,
                inferred: true,
                ownership: type.endsWith('*') ? 'managed owner returned by factory' : 'inferred local value',
                cleanup: true,
                factory: member.fullName
            });
        }
    }
    candidates.sort((left, right) => left.offset - right.offset);
    resolving.delete(variableName);
    return candidates.pop();
}

function entryForWord(index, text, position, word) {
    const exact = index.hover.get(word.full);
    if (exact) return exact;
    const segments = word.full.split('.');
    if (segments.length > 1) {
        const memberName = segments[segments.length - 1];
        const qualifier = segments[segments.length - 2];
        const variable = variableInfo(index, text, position, qualifier);
        if (variable && variable.type) {
            const owner = variable.type.replace(/^(?:raw|weak)\s+/, '').replace(/\*$/, '');
            const member = index.hover.get(`${owner}.${memberName}`);
            if (member) return member;
        }
    }
    return index.hover.get(word.name) || index.hover.get(word.name.split('.').pop());
}

function hoverFor(index, text, position) {
    const word = wordAt(text, position);
    if (!word) return undefined;
    const variable = variableInfo(index, text, position, word.name);
    if (variable) {
        const lines = [
            `\`\`\`absolute\n${variable.type} ${word.name}\n\`\`\``,
            `**${variable.inferred ? 'Inferred type' : 'Declared type'}:** \`${variable.type}\``,
            `**Ownership:** ${variable.ownership}`
        ];
        if (variable.cleanup)
            lines.push('**Lifetime:** automatic cleanup at scope exit, including early `return`/`break`.');
        if (variable.factory) lines.push(`Created by \`${variable.factory}\`.`);
        return {
            contents: { kind: 'markdown', value: lines.join('\n\n') },
            range: word.range
        };
    }

    const entry = entryForWord(index, text, position, word);
    if (!entry) {
        const core = CORE_HOVER[word.name] ||
            (CORE_TYPES.includes(word.name)
                ? [`built-in ${word.name} type`, 'Built-in Absolute value type.']
                : undefined);
        if (core) {
            return {
                contents: {
                    kind: 'markdown',
                    value: `\`\`\`absolute\n${core[0]}\n\`\`\`\n\n${core[1]}\n\n_Core language_`
                },
                range: word.range
            };
        }
        const signature = declarationSignatureAt(text, word.name);
        if (!signature) return undefined;
        return {
            contents: {
                kind: 'markdown',
                value: `\`\`\`absolute\n${signature}\n\`\`\`\n\n_Local declaration_`
            },
            range: word.range
        };
    }

    const sections = [`\`\`\`absolute\n${entry.detail}\n\`\`\``];
    if (entry.documentation) sections.push(entry.documentation);
    if (entry.owner) sections.push(`**Owner:** \`${entry.owner}\``);
    if (entry.ownership) sections.push(`**Ownership:** ${entry.ownership}`);
    const members = index.membersByOwner.get(entry.fullName) || [];
    if (members.length) {
        const visible = members.slice(0, 12).map(member => `- \`${member.detail}\``).join('\n');
        sections.push(`**Members (${members.length}):**\n\n${visible}` +
            (members.length > 12 ? `\n- _…and ${members.length - 12} more_` : ''));
    }
    if (entry.kind === 'type' && /\bRAII\b|scope exit|automatically/i.test(entry.documentation))
        sections.push('**Lifetime:** managed owners are released automatically at scope exit.');
    if (entry.since) sections.push(`**Since:** ${entry.since}`);
    sections.push(`_${entry.kind} from plugin \`${entry.plugin}\`_`);
    const value = {
        contents: {
            kind: 'markdown',
            value: sections.join('\n\n')
        },
        range: word.range
    };
    return value;
}

function signatureHelpFor(index, text, position) {
    const offsets = lineOffsets(text);
    const offset = positionToOffset(offsets, position, text.length);
    const before = stripComments(text.slice(0, offset));
    const match = /([A-Za-z_][A-Za-z0-9_.]*)\s*\(([^()]*)$/.exec(before);
    if (!match) return undefined;
    const callable = match[1];
    const segments = callable.split('.');
    const name = segments[segments.length - 1];
    let entry = index.hover.get(callable);
    if (!entry && segments.length > 1) {
        const qualifier = segments[segments.length - 2];
        const variable = variableInfo(index, text, position, qualifier);
        const owner = variable && variable.type
            ? variable.type.replace(/^(?:raw|weak)\s+/, '').replace(/\*$/, '')
            : segments.slice(0, -1).join('.');
        entry = index.hover.get(`${owner}.${name}`);
    }
    if (!entry) entry = index.hover.get(name);
    if (!entry || !entry.detail.includes('(')) return undefined;

    const open = entry.detail.indexOf('(');
    const close = entry.detail.lastIndexOf(')');
    const parameterText = close > open ? entry.detail.slice(open + 1, close).trim() : '';
    const parameters = parameterText
        ? parameterText.split(',').map(parameter => ({
            label: parameter.trim(),
            documentation: `Parameter \`${parameter.trim()}\``
        }))
        : [];
    const activeParameter = Math.min(
        parameters.length ? parameters.length - 1 : 0,
        (match[2].match(/,/g) || []).length
    );
    return {
        signatures: [{
            label: entry.detail,
            documentation: entry.documentation || `From plugin ${entry.plugin}`,
            parameters
        }],
        activeSignature: 0,
        activeParameter
    };
}

/**
 * Extract opaque plugin blocks (e.g. shader Stage { ... }) for virtual documents
 * and breakpoint source mapping.
 *
 * @param {string} hostUri  file:// URI of the host .abs
 * @param {string} text     host source
 * @param {string[]} tags   block introducers (default: shader)
 */
function extractOpaqueBlocks(hostUri, text, tags = ['shader']) {
    const clean = stripComments(text);
    const offsets = lineOffsets(text);
    const blocks = [];
    const tagSet = new Set(tags.map(t => t.toLowerCase()));

    let i = 0;
    while (i < clean.length) {
        // Match tag at identifier boundary
        const rest = clean.slice(i);
        const match = /^([A-Za-z_][A-Za-z0-9_]*)\b/.exec(rest);
        if (!match || !tagSet.has(match[1].toLowerCase())) {
            i += 1;
            continue;
        }
        const tag = match[1];
        const tagStart = i;
        i += tag.length;
        while (i < clean.length && /\s/.test(clean[i])) i += 1;

        // Optional stage / name identifier
        let name = '';
        const nameMatch = /^([A-Za-z_][A-Za-z0-9_]*)\b/.exec(clean.slice(i));
        if (nameMatch) {
            name = nameMatch[1];
            i += name.length;
            while (i < clean.length && /\s/.test(clean[i])) i += 1;
        }
        if (clean[i] !== '{') continue;
        const bodyOpen = i;
        i += 1;
        let depth = 1;
        while (i < clean.length && depth > 0) {
            if (clean[i] === '{') depth += 1;
            else if (clean[i] === '}') depth -= 1;
            i += 1;
        }
        const bodyClose = i; // exclusive, past closing brace
        const hostStart = offsetToPosition(offsets, tagStart);
        const hostEnd = offsetToPosition(offsets, bodyClose);
        const bodyText = text.slice(bodyOpen + 1, bodyClose - 1);
        const virtualUri = `${hostUri.replace(/^file:/, 'absolute-opaque:')}#${tag.toLowerCase()}@${hostStart.line}`;
        blocks.push({
            tag: tag.toLowerCase(),
            name,
            hostUri,
            virtualUri,
            hostRange: { start: hostStart, end: hostEnd },
            // Line in host that breakpoints map to (start of block).
            hostBreakpointLine: hostStart.line,
            body: bodyText.replace(/^\r?\n/, '').replace(/\r?\n\s*$/, '\n'),
            language: tag.toLowerCase() === 'shader' ? 'hlsl' : 'plaintext',
            mappings: [{
                generatedLine: 0,
                generatedCharacter: 0,
                originalLine: hostStart.line,
                originalCharacter: hostStart.character,
                source: hostUri
            }]
        });
    }
    return blocks;
}

function opaqueVirtualUri(hostPath, tag, startLine) {
    const hostUri = pathToUri(hostPath);
    return `${hostUri.replace(/^file:/, 'absolute-opaque:')}#${tag}@${startLine}`;
}

function parseOpaqueVirtualUri(uri) {
    // absolute-opaque:/C:/path/file.abs#shader@12  or absolute-opaque:///C:/...
    if (!uri || !uri.startsWith('absolute-opaque:')) return undefined;
    const withoutScheme = uri.slice('absolute-opaque:'.length);
    const hash = withoutScheme.lastIndexOf('#');
    if (hash < 0) return undefined;
    let hostPart = withoutScheme.slice(0, hash);
    const frag = withoutScheme.slice(hash + 1);
    const fragMatch = /^([A-Za-z_][A-Za-z0-9_]*)@(\d+)$/.exec(frag);
    if (!fragMatch) return undefined;
    // Restore file URI form for path conversion
    const hostUri = hostPart.startsWith('/') || /^[A-Za-z]:/.test(hostPart)
        ? `file://${hostPart.startsWith('/') ? hostPart : '/' + hostPart}`
        : `file://${hostPart}`;
    return {
        hostUri,
        hostPath: uriToPath(hostUri),
        tag: fragMatch[1],
        startLine: parseInt(fragMatch[2], 10)
    };
}

function evaluateExpressionSource(expression, preamble = '') {
    const expr = String(expression || '').trim();
    if (!expr) throw new Error('empty expression');
    // Statement-like: ends with ; or starts with keyword
    const isStatement = /[;{}]\s*$/.test(expr) ||
        /^(if|for|while|return|throw|try|println|print|assert|var|let|auto|defer|spawn)\b/.test(expr);
    let body;
    if (isStatement) {
        body = expr.endsWith(';') || expr.endsWith('}') ? expr : expr + ';';
    } else {
        // Print the expression result; format supports ints/bools/strings/pointers.
        body = `println(format("{}", (${expr})));`;
    }
    return `${preamble}\n\nint32 main() {\n    ${body}\n    return 0;\n}\n`;
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
    signatureHelpFor,
    semanticTokens,
    extractOpaqueBlocks,
    opaqueVirtualUri,
    parseOpaqueVirtualUri,
    evaluateExpressionSource,
    lineOffsets,
    offsetToPosition,
    positionToOffset
};
