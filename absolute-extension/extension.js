'use strict';

const vscode = require('vscode');
const fs = require('fs');
const path = require('path');
const childProcess = require('child_process');
const { LspClient } = require('./client/lsp-client');

let statusBar;
let output;
let lspClient;
let pluginIndexState = { plugins: [], errors: [] };

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

function collectPluginPaths() {
    const plugins = [];
    const editorMetadata = [];
    const folders = vscode.workspace.workspaceFolders || [];
    for (const folder of folders) {
        const configuration = vscode.workspace.getConfiguration('absolute', folder.uri);
        for (const item of configuration.get('plugins', []))
            plugins.push(resolveConfiguredPath(item, folder.uri.fsPath, folder));
        for (const item of configuration.get('editorMetadata', []))
            editorMetadata.push(resolveConfiguredPath(item, folder.uri.fsPath, folder));
        try {
            const projects = fs.readdirSync(folder.uri.fsPath)
                .filter(name => name.endsWith('.absproj'))
                .map(name => path.join(folder.uri.fsPath, name));
            // shallow only for bootstrap; recursive discovery happens in refreshPlugins
            for (const projectFile of projects) {
                try {
                    const project = readJson(projectFile);
                    const root = path.dirname(projectFile);
                    for (const item of Array.isArray(project.plugins) ? project.plugins : [])
                        plugins.push(path.resolve(root, item));
                } catch (_) { /* ignore */ }
            }
        } catch (_) { /* ignore */ }
    }
    return {
        plugins: [...new Set(plugins.map(item => path.resolve(item)))],
        editorMetadata: [...new Set(editorMetadata.map(item => path.resolve(item)))]
    };
}

async function refreshPlugins(showMessage = false) {
    const folders = vscode.workspace.workspaceFolders || [];
    const plugins = [];
    const editorMetadata = [];
    const pluginRecords = [];
    const errors = [];

    for (const folder of folders) {
        const configuration = vscode.workspace.getConfiguration('absolute', folder.uri);
        const configuredSearch = configuration.get('pluginSearchPaths', [])
            .map(item => resolveConfiguredPath(item, folder.uri.fsPath, folder));
        const roots = configuration.get('plugins', [])
            .map(item => resolveConfiguredPath(item, folder.uri.fsPath, folder));
        for (const item of configuration.get('editorMetadata', []))
            editorMetadata.push(resolveConfiguredPath(item, folder.uri.fsPath, folder));

        const projectPattern = new vscode.RelativePattern(folder, '**/*.absproj');
        const projects = await vscode.workspace.findFiles(projectPattern,
            '**/{.git,node_modules,build,.absolute,.desktop-build}/**');
        for (const projectUri of projects) {
            try {
                const project = readJson(projectUri.fsPath);
                const root = path.dirname(projectUri.fsPath);
                for (const item of Array.isArray(project.pluginSearchPaths) ? project.pluginSearchPaths : [])
                    configuredSearch.push(path.resolve(root, item));
                for (const item of Array.isArray(project.plugins) ? project.plugins : [])
                    roots.push(path.resolve(root, item));
            } catch (error) {
                errors.push(`${projectUri.fsPath}: ${error.message || error}`);
            }
        }

        for (const root of [...new Set(roots)]) {
            plugins.push(root);
            if (root.endsWith('.absplugin') && fs.existsSync(root)) {
                try {
                    const manifest = readJson(root);
                    pluginRecords.push({
                        name: manifest.name || path.basename(root),
                        version: manifest.version || '?',
                        abi: manifest.abi,
                        manifest: root,
                        library: manifest.library
                            ? path.resolve(path.dirname(root), manifest.library)
                            : '',
                        capabilities: Array.isArray(manifest.provides) ? manifest.provides : []
                    });
                } catch (error) {
                    errors.push(`${root}: ${error.message || error}`);
                }
            }
        }
    }

    pluginIndexState = { plugins: pluginRecords, errors };
    statusBar.text = errors.length
        ? `$(warning) Absolute plugins: ${pluginRecords.length}`
        : `$(extensions) Absolute plugins: ${pluginRecords.length}`;
    statusBar.tooltip = errors.length
        ? `${errors.length} plugin metadata problem(s). Click for details.`
        : `${pluginRecords.length} plugin(s) loaded. Click for details.`;
    statusBar.show();

    if (lspClient && lspClient.ready) {
        const configuration = vscode.workspace.getConfiguration('absolute');
        await lspClient.setConfiguration({
            compilerPath: configuration.get('compilerPath', 'absolutec'),
            compilerArguments: configuration.get('compilerArguments', []),
            plugins,
            editorMetadata
        });
    }

    if (showMessage) {
        vscode.window.showInformationMessage(
            `Absolute: loaded ${pluginRecords.length} plugin(s) via LSP.`);
    }
}

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
    output.appendLine(`Absolute plugins: ${pluginIndexState.plugins.length}`);
    for (const plugin of pluginIndexState.plugins) {
        output.appendLine(`\n${plugin.name} v${plugin.version} (ABI ${plugin.abi})`);
        if (plugin.manifest) output.appendLine(`  manifest: ${plugin.manifest}`);
        if (plugin.library) output.appendLine(`  library: ${plugin.library}`);
        if (plugin.capabilities?.length) output.appendLine(`  provides: ${plugin.capabilities.join(', ')}`);
    }
    if (pluginIndexState.errors.length) {
        output.appendLine('\nProblems:');
        for (const problem of pluginIndexState.errors) output.appendLine(`  - ${problem}`);
    }
    output.appendLine('\nLanguage intelligence is provided by absolute-lsp (server/lsp-server.js).');
    output.show(true);
}

async function activate(context) {
    output = vscode.window.createOutputChannel('Absolute');
    statusBar = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 50);
    statusBar.command = 'absolute.showPluginInfo';
    context.subscriptions.push(output, statusBar);

    const configuration = vscode.workspace.getConfiguration('absolute');
    const boot = collectPluginPaths();
    lspClient = new LspClient();
    await lspClient.start(context, {
        compilerPath: configuration.get('compilerPath', 'absolutec'),
        compilerArguments: configuration.get('compilerArguments', []),
        plugins: boot.plugins,
        editorMetadata: boot.editorMetadata
    });
    context.subscriptions.push({ dispose: () => lspClient.stop() });

    context.subscriptions.push(
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
                    artifact = {
                        executable: program, input: launch.source || program,
                        workspace: expandVariables(launch.cwd || folder?.uri.fsPath || path.dirname(program), folder, program),
                        folder
                    };
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
        if (event.affectsConfiguration('absolute'))
            refreshPlugins(false);
    }));
    await refreshPlugins(false);
}

function deactivate() {
    return lspClient ? lspClient.stop() : undefined;
}

module.exports = { activate, deactivate };
