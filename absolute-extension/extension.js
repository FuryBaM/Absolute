'use strict';

const vscode = require('vscode');
const fs = require('fs');
const path = require('path');
const childProcess = require('child_process');

const coreKeywords = [
    'if', 'else', 'switch', 'case', 'default', 'for', 'while', 'foreach', 'in', 'do',
    'break', 'continue', 'new', 'delete', 'namespace', 'import', 'return', 'true', 'false',
    'null', 'class', 'struct', 'enum', 'group', 'this', 'public', 'private', 'protected',
    'sealed', 'internal', 'virtual', 'override', 'const', 'static', 'auto', 'async', 'await',
    'catch', 'finally', 'try', 'throw', 'yield', 'get', 'set', 'operator', 'extension',
    'extern', 'export', 'raw', 'ref'
];
const coreTypes = [
    'int8', 'int16', 'int32', 'int64', 'uint8', 'uint16', 'uint32', 'uint64',
    'float', 'double', 'char', 'bool', 'string', 'void', 'dynamic'
];

let index = emptyIndex();
let diagnostics;
let statusBar;
let output;
const semanticEmitter = new vscode.EventEmitter();

function emptyIndex() {
    return {
        plugins: [],
        entries: [],
        hover: new Map(),
        keywordNames: new Set(),
        typeNames: new Set(),
        functionNames: new Set(),
        namespaceNames: new Set(),
        errors: []
    };
}

function canonical(file) {
    const resolved = path.resolve(file);
    return process.platform === 'win32' ? resolved.toLowerCase() : resolved;
}

function readJson(file) {
    const text = fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, '');
    return JSON.parse(text);
}

function expandVariables(value, folder, file) {
    if (typeof value !== 'string') return value;
    const editorFile = vscode.window.activeTextEditor ? vscode.window.activeTextEditor.document.uri.fsPath : '';
    const activeFile = file && !String(file).includes('${') ? file : editorFile;
    // VS Code allows opening a single file without a workspace folder. In that
    // mode ${workspaceFolder} must still expand to a useful absolute path;
    // otherwise the default output becomes `\\.absolute\\bin` on Windows.
    const workspace = folder ? folder.uri.fsPath : (activeFile ? path.dirname(activeFile) : '');
    return value
        .replace(/\$\{workspaceFolder\}/g, workspace)
        .replace(/\$\{file\}/g, activeFile)
        .replace(/\$\{fileDirname\}/g, activeFile ? path.dirname(activeFile) : workspace)
        .replace(/\$\{fileBasenameNoExtension\}/g, activeFile ? path.basename(activeFile, path.extname(activeFile)) : 'absolute');
}

function nearestFolderSettings(file) {
    if (!file || !path.isAbsolute(file)) return undefined;
    let directory = path.dirname(file);
    while (directory) {
        const settingsFile = path.join(directory, '.vscode', 'settings.json');
        if (fs.existsSync(settingsFile)) {
            try { return { root: directory, values: readJson(settingsFile) }; }
            catch (error) {
                output?.appendLine(`Cannot read ${settingsFile}: ${error.message || error}`);
                return undefined;
            }
        }
        const parent = path.dirname(directory);
        if (parent === directory) break;
        directory = parent;
    }
    return undefined;
}

function configuredValue(configuration, localSettings, name, fallback) {
    const key = `absolute.${name}`;
    if (localSettings && Object.prototype.hasOwnProperty.call(localSettings, key)) return localSettings[key];
    return configuration.get(name, fallback);
}

function resolveConfiguredPath(value, base, folder, file) {
    const expanded = expandVariables(value, folder, file);
    return path.isAbsolute(expanded) ? path.normalize(expanded) : path.resolve(base, expanded);
}

function diagnostic(file, message, severity = vscode.DiagnosticSeverity.Error) {
    const uri = vscode.Uri.file(file);
    const item = new vscode.Diagnostic(new vscode.Range(0, 0, 0, 1), message, severity);
    item.source = 'Absolute plugins';
    const existing = diagnostics.get(uri) || [];
    diagnostics.set(uri, existing.concat(item));
    index.errors.push(`${file}: ${message}`);
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

function addMetadata(metadata, pluginName) {
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

function dependencyCandidates(name) {
    const dashed = name.replace(/\./g, '-');
    return [...new Set([`${name}.absplugin`, `${dashed}.absplugin`])];
}

function findDependency(name, directories) {
    for (const directory of directories) {
        for (const filename of dependencyCandidates(name)) {
            const candidate = path.join(directory, filename);
            if (fs.existsSync(candidate)) return candidate;
        }
    }
    return undefined;
}

function loadEditorMetadata(manifest, manifestFile) {
    let metadata = manifest.editor;
    let metadataFile;
    if (typeof metadata === 'string') {
        metadataFile = path.resolve(path.dirname(manifestFile), metadata);
        if (!fs.existsSync(metadataFile)) throw new Error(`editor metadata does not exist: ${metadataFile}`);
        metadata = readJson(metadataFile);
    } else if (!metadata) {
        const stem = manifestFile.slice(0, -'.absplugin'.length);
        const inferred = `${stem}.editor.json`;
        if (fs.existsSync(inferred)) {
            metadataFile = inferred;
            metadata = readJson(inferred);
        }
    }
    if (metadata) addMetadata(metadata, manifest.name || path.basename(manifestFile));
    return metadataFile;
}

function loadManifest(manifestFile, searchPaths, states, stack, requestedName) {
    const absolute = path.resolve(manifestFile);
    const key = canonical(absolute);
    if (states.get(key) === 'loaded') return;
    if (states.get(key) === 'visiting') {
        const cycle = stack.concat(absolute).map(file => path.basename(file)).join(' -> ');
        diagnostic(absolute, `Plugin dependency cycle: ${cycle}`);
        return;
    }
    if (!fs.existsSync(absolute)) {
        diagnostic(stack[stack.length - 1] || absolute, `Plugin manifest does not exist: ${absolute}`);
        return;
    }

    states.set(key, 'visiting');
    stack.push(absolute);
    try {
        const manifest = readJson(absolute);
        if (!manifest.name || !manifest.version || manifest.abi === undefined || !manifest.library)
            throw new Error('manifest requires name, version, abi, and library');
        if (requestedName && requestedName !== manifest.name)
            throw new Error(`dependency expected '${requestedName}', manifest declares '${manifest.name}'`);

        const record = {
            name: manifest.name,
            version: manifest.version,
            abi: manifest.abi,
            manifest: absolute,
            library: path.resolve(path.dirname(absolute), manifest.library),
            capabilities: Array.isArray(manifest.provides) ? manifest.provides : [],
            metadata: undefined
        };
        index.plugins.push(record);

        const directories = [path.dirname(absolute), ...searchPaths];
        const dependencies = manifest.dependencies || {};
        if (Array.isArray(dependencies)) {
            for (const dependency of dependencies) {
                if (!dependency || !dependency.name) continue;
                const dependencyFile = dependency.path
                    ? path.resolve(path.dirname(absolute), dependency.path)
                    : findDependency(dependency.name, directories);
                if (!dependencyFile) diagnostic(absolute, `Cannot resolve plugin dependency '${dependency.name}'`);
                else loadManifest(dependencyFile, searchPaths, states, stack, dependency.name);
            }
        } else if (typeof dependencies === 'object') {
            for (const dependencyName of Object.keys(dependencies)) {
                const dependencyFile = findDependency(dependencyName, directories);
                if (!dependencyFile) diagnostic(absolute, `Cannot resolve plugin dependency '${dependencyName}'`);
                else loadManifest(dependencyFile, searchPaths, states, stack, dependencyName);
            }
        }
        record.metadata = loadEditorMetadata(manifest, absolute);
        states.set(key, 'loaded');
    } catch (error) {
        diagnostic(absolute, error.message || String(error));
        states.delete(key);
    } finally {
        stack.pop();
    }
}

function loadRootPlugin(pluginPath, searchPaths, states) {
    const absolute = path.resolve(pluginPath);
    if (path.extname(absolute).toLowerCase() === '.absplugin') {
        loadManifest(absolute, searchPaths, states, []);
        return;
    }
    const adjacentManifest = absolute.replace(/\.(dll|so|dylib)$/i, '.absplugin');
    if (fs.existsSync(adjacentManifest)) {
        loadManifest(adjacentManifest, searchPaths, states, []);
        return;
    }
    const sidecar = absolute.replace(/\.(dll|so|dylib)$/i, '.editor.json');
    const name = path.basename(absolute, path.extname(absolute));
    const record = { name, version: 'unknown', abi: 'unknown', library: absolute, manifest: undefined, capabilities: [] };
    index.plugins.push(record);
    if (fs.existsSync(sidecar)) {
        try { addMetadata(readJson(sidecar), name); record.metadata = sidecar; }
        catch (error) { diagnostic(sidecar, error.message || String(error)); }
    } else {
        index.errors.push(`${absolute}: native plugin has no readable manifest/editor sidecar`);
    }
}

function loadStandaloneMetadata(metadataFile) {
    const absolute = path.resolve(metadataFile);
    try {
        if (!fs.existsSync(absolute)) throw new Error(`editor metadata does not exist: ${absolute}`);
        const metadata = readJson(absolute);
        const pluginName = metadata.plugin || path.basename(absolute).replace(/\.editor\.json$/i, '');
        addMetadata(metadata, pluginName);
        index.plugins.push({
            name: pluginName, version: 'editor-only', abi: 'n/a', manifest: undefined,
            library: 'not enabled for build', capabilities: [], metadata: absolute
        });
    } catch (error) {
        diagnostic(absolute, error.message || String(error));
    }
}

async function refreshPlugins(showMessage = false) {
    index = emptyIndex();
    diagnostics.clear();
    const states = new Map();
    const folders = vscode.workspace.workspaceFolders || [];

    for (const folder of folders) {
        const configuration = vscode.workspace.getConfiguration('absolute', folder.uri);
        const configuredSearch = configuration.get('pluginSearchPaths', []);
        const searchPaths = configuredSearch.map(item => resolveConfiguredPath(item, folder.uri.fsPath, folder));
        const roots = configuration.get('plugins', []).map(item => resolveConfiguredPath(item, folder.uri.fsPath, folder));
        const editorMetadata = configuration.get('editorMetadata', [])
            .map(item => resolveConfiguredPath(item, folder.uri.fsPath, folder));

        const projectPattern = new vscode.RelativePattern(folder, '**/*.absproj');
        const projects = await vscode.workspace.findFiles(projectPattern, '**/{.git,node_modules,build,.absolute,.desktop-build}/**');
        for (const projectUri of projects) {
            try {
                const project = readJson(projectUri.fsPath);
                const root = path.dirname(projectUri.fsPath);
                for (const item of Array.isArray(project.pluginSearchPaths) ? project.pluginSearchPaths : [])
                    searchPaths.push(path.resolve(root, item));
                for (const item of Array.isArray(project.plugins) ? project.plugins : [])
                    roots.push(path.resolve(root, item));
            } catch (error) {
                diagnostic(projectUri.fsPath, `Cannot read project plugins: ${error.message || error}`);
            }
        }
        for (const root of [...new Set(roots.map(canonical))]) loadRootPlugin(root, searchPaths, states);
        for (const metadataFile of [...new Set(editorMetadata.map(canonical))]) loadStandaloneMetadata(metadataFile);
    }

    const unique = new Map();
    for (const plugin of index.plugins) if (!unique.has(plugin.name)) unique.set(plugin.name, plugin);
    index.plugins = [...unique.values()].sort((a, b) => a.name.localeCompare(b.name));
    statusBar.text = index.errors.length
        ? `$(warning) Absolute plugins: ${index.plugins.length}`
        : `$(extensions) Absolute plugins: ${index.plugins.length}`;
    statusBar.tooltip = index.errors.length
        ? `${index.errors.length} plugin metadata problem(s). Click for details.`
        : `${index.plugins.length} plugin(s) loaded. Click for details.`;
    statusBar.show();
    semanticEmitter.fire();
    if (showMessage) vscode.window.showInformationMessage(
        `Absolute: loaded ${index.plugins.length} plugin(s), ${index.entries.length} editor symbol(s).`);
}

function completionKind(kind) {
    return kind === 'keyword' ? vscode.CompletionItemKind.Keyword :
        kind === 'type' ? vscode.CompletionItemKind.Class :
        kind === 'namespace' ? vscode.CompletionItemKind.Module :
        kind === 'snippet' ? vscode.CompletionItemKind.Snippet :
        kind === 'member' ? vscode.CompletionItemKind.Method : vscode.CompletionItemKind.Function;
}

function completionItem(entry, qualifier) {
    let label = entry.fullName;
    let snippet = entry.snippet;
    if (qualifier && entry.fullName.startsWith(`${qualifier}.`)) {
        label = entry.fullName.slice(qualifier.length + 1);
        if (snippet && snippet.startsWith(`${qualifier}.`)) snippet = snippet.slice(qualifier.length + 1);
    } else if (qualifier && entry.kind === 'member') label = entry.name;
    const item = new vscode.CompletionItem(label, completionKind(entry.kind));
    item.detail = `${entry.detail} — ${entry.plugin}`;
    item.documentation = new vscode.MarkdownString(entry.documentation || '');
    item.sortText = entry.kind === 'snippet' ? `0-${label}` : `1-${label}`;
    if (snippet) {
        item.insertText = new vscode.SnippetString(snippet);
    } else if (qualifier) item.insertText = label;
    return item;
}

const completionProvider = {
    provideCompletionItems(document, position) {
        const before = document.lineAt(position.line).text.slice(0, position.character);
        const qualified = before.match(/([A-Za-z_][A-Za-z0-9_.]*)\.$/);
        const qualifier = qualified ? qualified[1] : undefined;
        const items = [];
        if (!qualifier) {
            for (const keyword of coreKeywords) items.push(new vscode.CompletionItem(keyword, vscode.CompletionItemKind.Keyword));
            for (const type of coreTypes) items.push(new vscode.CompletionItem(type, vscode.CompletionItemKind.TypeParameter));
        }
        for (const entry of index.entries) {
            if (!qualifier || entry.fullName.startsWith(`${qualifier}.`) || entry.kind === 'member')
                items.push(completionItem(entry, qualifier));
        }
        return items;
    }
};

const hoverProvider = {
    provideHover(document, position) {
        const range = document.getWordRangeAtPosition(position, /[A-Za-z_][A-Za-z0-9_.]*/);
        if (!range) return undefined;
        const name = document.getText(range);
        const entry = index.hover.get(name) || index.hover.get(name.split('.').pop());
        if (!entry) return undefined;
        const markdown = new vscode.MarkdownString();
        markdown.appendCodeblock(entry.detail, 'absolute');
        if (entry.documentation) markdown.appendMarkdown(`\n${entry.documentation}\n`);
        markdown.appendMarkdown(`\n_Plugin: ${entry.plugin}_`);
        return new vscode.Hover(markdown, range);
    }
};

const semanticLegend = new vscode.SemanticTokensLegend(['keyword', 'type', 'function', 'namespace']);
const semanticProvider = {
    onDidChangeSemanticTokens: semanticEmitter.event,
    provideDocumentSemanticTokens(document) {
        const builder = new vscode.SemanticTokensBuilder(semanticLegend);
        let blockComment = false;
        for (let lineNumber = 0; lineNumber < document.lineCount; ++lineNumber) {
            const text = document.lineAt(lineNumber).text;
            for (let offset = 0; offset < text.length;) {
                if (blockComment) {
                    const end = text.indexOf('*/', offset);
                    if (end < 0) break;
                    blockComment = false; offset = end + 2; continue;
                }
                if (text.startsWith('//', offset)) break;
                if (text.startsWith('/*', offset)) { blockComment = true; offset += 2; continue; }
                if (text[offset] === '"' || text[offset] === "'") {
                    const quote = text[offset++];
                    while (offset < text.length) {
                        if (text[offset] === '\\') offset += 2;
                        else if (text[offset++] === quote) break;
                    }
                    continue;
                }
                const match = /^[A-Za-z_][A-Za-z0-9_]*/.exec(text.slice(offset));
                if (!match) { ++offset; continue; }
                const word = match[0];
                let token;
                if (index.keywordNames.has(word)) token = 0;
                else if (index.typeNames.has(word)) token = 1;
                else if (index.functionNames.has(word)) token = 2;
                else if (index.namespaceNames.has(word)) token = 3;
                if (token !== undefined) builder.push(lineNumber, offset, word.length, token, 0);
                offset += word.length;
            }
        }
        return builder.build();
    }
};

async function nearestProject(source, folder) {
    let directory = path.dirname(source);
    const boundary = folder ? canonical(folder.uri.fsPath) : undefined;
    while (directory && (!boundary || canonical(directory).startsWith(boundary))) {
        try {
            const projects = fs.readdirSync(directory).filter(name => name.endsWith('.absproj')).sort();
            if (projects.length) return path.join(directory, projects[0]);
        } catch (_) { }
        const parent = path.dirname(directory);
        if (parent === directory) break;
        directory = parent;
    }
    return undefined;
}

function shellQuote(value) {
    if (process.platform === 'win32') return `"${String(value).replace(/"/g, '\\"')}"`;
    return `'${String(value).replace(/'/g, `'\\''`)}'`;
}

async function executeTaskAndWait(task) {
    let execution;
    const completion = new Promise(resolve => {
        const subscription = vscode.tasks.onDidEndTaskProcess(event => {
            if (event.execution === execution) { subscription.dispose(); resolve(event.exitCode); }
        });
    });
    execution = await vscode.tasks.executeTask(task);
    return completion;
}

function executeProcessAndWait(command, args, cwd, useShell = false) {
    return new Promise(resolve => {
        const printable = useShell ? command : [command, ...args].map(shellQuote).join(' ');
        output.appendLine(`> ${printable}`);
        output.show(true);
        let settled = false;
        const child = childProcess.spawn(command, args, {
            cwd,
            shell: useShell,
            windowsHide: false,
            env: process.env
        });
        child.stdout.on('data', data => output.append(data.toString()));
        child.stderr.on('data', data => output.append(data.toString()));
        child.on('error', error => {
            output.appendLine(`Absolute process error: ${error.message}`);
            if (!settled) { settled = true; resolve(undefined); }
        });
        child.on('close', code => {
            output.appendLine(`\nProcess exited with code ${code}.`);
            if (!settled) { settled = true; resolve(code); }
        });
    });
}

async function buildAbsolute(requestedSource) {
    const editor = vscode.window.activeTextEditor;
    let source = requestedSource;
    if (!source && editor && (editor.document.languageId === 'absolute' ||
        editor.document.languageId === 'absolute-project' || path.extname(editor.document.uri.fsPath) === '.absproj'))
        source = editor.document.uri.fsPath;
    const folder = source ? vscode.workspace.getWorkspaceFolder(vscode.Uri.file(source)) :
        (vscode.workspace.workspaceFolders || [])[0];
    if (!source) {
        vscode.window.showErrorMessage('Absolute: open an .abs file or specify source in launch.json.');
        return undefined;
    }
    source = expandVariables(source, folder, source);
    if (!path.isAbsolute(source)) {
        const base = folder ? folder.uri.fsPath :
            (editor && editor.document.uri.scheme === 'file' ? path.dirname(editor.document.uri.fsPath) : process.cwd());
        source = path.resolve(base, source);
    }
    if (!fs.existsSync(source)) {
        vscode.window.showErrorMessage(`Absolute input does not exist: ${source}`);
        return undefined;
    }

    const configuration = vscode.workspace.getConfiguration('absolute', folder ? folder.uri : undefined);
    // Also look above an opened project folder. This is useful in a monorepo:
    // opening Demo/Demo.absproj must still inherit the repository toolchain.
    const localContext = nearestFolderSettings(source);
    const localSettings = localContext?.values;
    const variableFolder = localContext ? { uri: vscode.Uri.file(localContext.root) } : folder;
    let input = source;
    if (configuredValue(configuration, localSettings, 'preferProject', true) && path.extname(source) === '.abs')
        input = await nearestProject(source, variableFolder) || source;
    let projectName = path.basename(input, path.extname(input));
    if (path.extname(input) === '.absproj') {
        try { projectName = readJson(input).name || projectName; } catch (_) { }
    }
    const workspace = variableFolder ? variableFolder.uri.fsPath : path.dirname(input);
    const outputDirectory = resolveConfiguredPath(
        configuredValue(configuration, localSettings, 'outputDirectory', '${workspaceFolder}/.absolute/bin'),
        workspace, variableFolder, source);
    fs.mkdirSync(outputDirectory, { recursive: true });
    const executable = path.join(outputDirectory, `${projectName}${process.platform === 'win32' ? '.exe' : ''}`);
    const configuredPlugins = configuredValue(configuration, localSettings, 'plugins', [])
        .map(item => resolveConfiguredPath(item, workspace, variableFolder, source));
    const searchPaths = configuredValue(configuration, localSettings, 'pluginSearchPaths', [])
        .map(item => resolveConfiguredPath(item, workspace, variableFolder, source));

    const pluginArguments = [];
    for (const plugin of configuredPlugins) pluginArguments.push('--plugin', plugin);
    for (const searchPath of searchPaths) pluginArguments.push('--plugin-path', searchPath);
    const customCommand = configuredValue(configuration, localSettings, 'buildCommand', '').trim();
    let execution;
    let directCommand;
    let directArguments = [];
    let directShell = false;
    if (customCommand) {
        const pluginText = pluginArguments.map(shellQuote).join(' ');
        const command = expandVariables(customCommand, variableFolder, source)
            .replace(/\{input\}/g, shellQuote(input))
            .replace(/\{output\}/g, shellQuote(executable))
            .replace(/\{workspace\}/g, shellQuote(workspace))
            .replace(/\{plugins\}/g, pluginText);
        // ShellExecution wraps the complete string in `cmd.exe /d /c "..."` on
        // Windows. A quoted .bat path then loses its quote boundary and cmd
        // reports a filename/directory syntax error. Node's shell spawn keeps
        // the command intact.
        directCommand = command;
        directShell = true;
    } else {
        const compiler = expandVariables(
            configuredValue(configuration, localSettings, 'compilerPath', 'absolutec'), variableFolder, source);
        const args = [];
        if (path.extname(input) === '.absproj') args.push('build');
        args.push(input, ...configuredValue(configuration, localSettings, 'compilerArguments', []), ...pluginArguments,
            '--build-exe', '-o', executable);
        if (folder) execution = new vscode.ProcessExecution(compiler, args, { cwd: workspace });
        else { directCommand = compiler; directArguments = args; }
    }
    let exitCode;
    if (!directCommand) {
        const task = new vscode.Task({ type: 'absolute', input }, folder,
            `Build ${projectName}`, 'Absolute', execution, []);
        task.presentationOptions = { reveal: vscode.TaskRevealKind.Always, panel: vscode.TaskPanelKind.Dedicated, clear: true };
        exitCode = await executeTaskAndWait(task);
    } else {
        output.clear();
        exitCode = await executeProcessAndWait(directCommand, directArguments, workspace, directShell);
    }
    if (exitCode !== 0 || !fs.existsSync(executable)) {
        vscode.window.showErrorMessage(`Absolute build failed${exitCode === undefined ? '' : ` with exit code ${exitCode}`}.`);
        return undefined;
    }
    return { executable, input, workspace, folder };
}

function projectFromUri(value) {
    const uri = value && value.fsPath ? value : undefined;
    return uri && path.extname(uri.fsPath).toLowerCase() === '.absproj' ? uri.fsPath : undefined;
}

function validateProject(projectFile) {
    const project = readJson(projectFile);
    if (!project.name || typeof project.name !== 'string') throw new Error("missing string property 'name'");
    if (!project.entry || typeof project.entry !== 'string') throw new Error("missing string property 'entry'");
    const entry = path.resolve(path.dirname(projectFile), project.entry);
    if (!fs.existsSync(entry)) throw new Error(`entry source does not exist: ${entry}`);
    return { project, entry };
}

async function chooseProject(resource, preferActive = true) {
    let projectFile = projectFromUri(resource);
    const activeFile = vscode.window.activeTextEditor?.document.uri.fsPath;
    if (!projectFile && preferActive && activeFile) {
        if (path.extname(activeFile).toLowerCase() === '.absproj') projectFile = activeFile;
        else if (path.extname(activeFile).toLowerCase() === '.abs')
            projectFile = await nearestProject(activeFile, vscode.workspace.getWorkspaceFolder(vscode.Uri.file(activeFile)));
    }

    if (!projectFile && vscode.workspace.workspaceFolders?.length) {
        const projects = await vscode.workspace.findFiles('**/*.absproj',
            '**/{.git,node_modules,build,.absolute,.desktop-build,.benchmark-build}/**');
        if (projects.length === 1) projectFile = projects[0].fsPath;
        else if (projects.length > 1) {
            const selected = await vscode.window.showQuickPick(projects.map(uri => ({
                label: path.basename(uri.fsPath),
                description: vscode.workspace.asRelativePath(uri),
                projectFile: uri.fsPath
            })), { placeHolder: 'Select an Absolute project' });
            projectFile = selected?.projectFile;
        }
    }

    if (!projectFile) {
        const selected = await vscode.window.showOpenDialog({
            canSelectFiles: true, canSelectFolders: false, canSelectMany: false,
            filters: { 'Absolute project': ['absproj'] },
            title: 'Open Absolute Project'
        });
        projectFile = selected?.[0]?.fsPath;
    }
    if (!projectFile) return undefined;
    try { validateProject(projectFile); }
    catch (error) {
        vscode.window.showErrorMessage(`Invalid Absolute project: ${error.message || error}`);
        return undefined;
    }
    return path.resolve(projectFile);
}

async function openProject(resource) {
    const projectFile = await chooseProject(resource, false);
    if (!projectFile) return;
    await vscode.commands.executeCommand('vscode.openFolder', vscode.Uri.file(path.dirname(projectFile)), false);
}

async function runProject(resource, debug = false) {
    const projectFile = await chooseProject(resource, true);
    if (!projectFile) return;
    const artifact = await buildAbsolute(projectFile);
    if (!artifact) return;
    if (debug) await startNativeDebug(artifact);
    else await runArtifact(artifact);
}

async function runArtifact(artifact, args = []) {
    if (!artifact.folder) {
        await executeProcessAndWait(artifact.executable, args, artifact.workspace, false);
        return;
    }
    const execution = new vscode.ProcessExecution(artifact.executable, args, { cwd: artifact.workspace });
    const task = new vscode.Task({ type: 'absolute-run' }, artifact.folder,
        `Run ${path.basename(artifact.executable)}`, 'Absolute', execution, []);
    task.presentationOptions = { reveal: vscode.TaskRevealKind.Always, panel: vscode.TaskPanelKind.Dedicated };
    await vscode.tasks.executeTask(task);
}

function nativeDebugger(configuration) {
    const selected = configuration.get('debugger', 'auto');
    if (selected !== 'auto') return selected;
    return process.platform === 'win32' ? 'cppvsdbg' : 'cppdbg';
}

async function startNativeDebug(artifact, launch = {}) {
    const configuration = vscode.workspace.getConfiguration('absolute', artifact.folder ? artifact.folder.uri : undefined);
    const debugConfiguration = {
        type: nativeDebugger(configuration), request: 'launch',
        name: launch.name || `Debug ${path.basename(artifact.executable)}`,
        program: artifact.executable, args: launch.args || [],
        cwd: expandVariables(launch.cwd || artifact.workspace, artifact.folder, artifact.input),
        stopAtEntry: launch.stopAtEntry !== false,
        externalConsole: configuration.get('externalConsole', false)
    };
    const started = await vscode.debug.startDebugging(artifact.folder, debugConfiguration);
    if (!started) vscode.window.showErrorMessage(
        `Absolute: could not start ${debugConfiguration.type}. Install/enable the Microsoft C/C++ extension or change absolute.debugger.`);
}

async function showPluginInfo() {
    output.clear();
    output.appendLine(`Absolute plugins: ${index.plugins.length}`);
    for (const plugin of index.plugins) {
        output.appendLine(`\n${plugin.name} v${plugin.version} (ABI ${plugin.abi})`);
        if (plugin.manifest) output.appendLine(`  manifest: ${plugin.manifest}`);
        output.appendLine(`  library: ${plugin.library}`);
        if (plugin.metadata) output.appendLine(`  editor: ${plugin.metadata}`);
        if (plugin.capabilities.length) output.appendLine(`  provides: ${plugin.capabilities.join(', ')}`);
    }
    if (index.errors.length) {
        output.appendLine('\nProblems:');
        for (const problem of index.errors) output.appendLine(`  - ${problem}`);
    }
    output.show(true);
}

function activate(context) {
    diagnostics = vscode.languages.createDiagnosticCollection('absolutePlugins');
    output = vscode.window.createOutputChannel('Absolute');
    statusBar = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 50);
    statusBar.command = 'absolute.showPluginInfo';
    context.subscriptions.push(diagnostics, output, statusBar, semanticEmitter);

    context.subscriptions.push(
        vscode.languages.registerCompletionItemProvider('absolute', completionProvider, '.'),
        vscode.languages.registerHoverProvider('absolute', hoverProvider),
        vscode.languages.registerDocumentSemanticTokensProvider({ language: 'absolute' }, semanticProvider, semanticLegend),
        vscode.commands.registerCommand('absolute.refreshPlugins', () => refreshPlugins(true)),
        vscode.commands.registerCommand('absolute.showPluginInfo', showPluginInfo),
        vscode.commands.registerCommand('absolute.openProject', openProject),
        vscode.commands.registerCommand('absolute.runProject', resource => runProject(resource, false)),
        vscode.commands.registerCommand('absolute.debugProject', resource => runProject(resource, true)),
        vscode.commands.registerCommand('absolute.runFile', async () => {
            const artifact = await buildAbsolute();
            if (artifact) await runArtifact(artifact);
        }),
        vscode.commands.registerCommand('absolute.debugFile', async () => {
            const artifact = await buildAbsolute();
            if (artifact) await startNativeDebug(artifact);
        }),
        vscode.debug.registerDebugConfigurationProvider('absolute', {
            async resolveDebugConfiguration(folder, launch) {
                let artifact;
                if (launch.noBuild && launch.program) {
                    const program = expandVariables(launch.program, folder, vscode.window.activeTextEditor?.document.uri.fsPath);
                    artifact = { executable: program, input: launch.source || program,
                        workspace: expandVariables(launch.cwd || folder?.uri.fsPath || path.dirname(program), folder, program), folder };
                } else artifact = await buildAbsolute(launch.source);
                if (artifact) setTimeout(() => startNativeDebug(artifact, launch), 0);
                return undefined;
            }
        })
    );

    for (const pattern of ['**/*.absproj', '**/*.absplugin', '**/*.editor.json']) {
        const watcher = vscode.workspace.createFileSystemWatcher(pattern);
        const refresh = () => refreshPlugins(false);
        watcher.onDidCreate(refresh); watcher.onDidChange(refresh); watcher.onDidDelete(refresh);
        context.subscriptions.push(watcher);
    }
    context.subscriptions.push(vscode.workspace.onDidChangeConfiguration(event => {
        if (event.affectsConfiguration('absolute.plugins') || event.affectsConfiguration('absolute.pluginSearchPaths') ||
            event.affectsConfiguration('absolute.editorMetadata'))
            refreshPlugins(false);
    }));
    refreshPlugins(false);
}

function deactivate() {}

module.exports = { activate, deactivate };
