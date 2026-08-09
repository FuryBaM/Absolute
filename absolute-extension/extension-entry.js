'use strict';

const vscode = require('vscode');
const fs = require('fs');
const path = require('path');
const baseExtension = require('./extension');
const toolchain = require('./toolchain');
const wasmTarget = require('./wasm-target');

const interceptedCommands = new Set([
    'absolute.runFile',
    'absolute.debugFile',
    'absolute.runProject',
    'absolute.debugProject'
]);

function readJson(file) {
    return JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
}

function expandVariables(value, folder, file) {
    if (typeof value !== 'string') return value;
    const workspace = folder ? folder.uri.fsPath : (file ? path.dirname(file) : '');
    return value
        .replace(/\$\{workspaceFolder\}/g, workspace)
        .replace(/\$\{file\}/g, file || '')
        .replace(/\$\{fileDirname\}/g, file ? path.dirname(file) : workspace)
        .replace(/\$\{fileBasenameNoExtension\}/g,
            file ? path.basename(file, path.extname(file)) : 'absolute');
}

function sourceFromRequest(requested) {
    if (requested && requested.fsPath) return requested.fsPath;
    if (typeof requested === 'string') return requested;
    return vscode.window.activeTextEditor?.document.uri.fsPath;
}

async function nearestProject(source, folder) {
    if (!source || path.extname(source).toLowerCase() !== '.abs') return undefined;
    let directory = path.dirname(source);
    const boundary = folder ? path.resolve(folder.uri.fsPath).toLowerCase() : undefined;
    while (directory && (!boundary || path.resolve(directory).toLowerCase().startsWith(boundary))) {
        try {
            const projects = fs.readdirSync(directory)
                .filter(name => name.toLowerCase().endsWith('.absproj'))
                .sort();
            if (projects.length) return path.join(directory, projects[0]);
        } catch (_) { }
        const parent = path.dirname(directory);
        if (parent === directory) break;
        directory = parent;
    }
    return undefined;
}

function contextRoots(folder, source) {
    return toolchain.candidateRoots([
        source,
        folder && folder.uri && folder.uri.fsPath,
        ...(vscode.workspace.workspaceFolders || []).map(item => item.uri.fsPath),
        path.resolve(__dirname, '..')
    ]);
}

function resolveConfiguredPath(value, base, folder, source) {
    const expanded = expandVariables(String(value), folder, source);
    return path.isAbsolute(expanded) ? path.normalize(expanded) : path.resolve(base, expanded);
}

async function describeWasmBuild(requested, forceWasm) {
    let source = sourceFromRequest(requested);
    if (!source) return undefined;
    source = path.resolve(source);
    if (!fs.existsSync(source)) return undefined;

    const folder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(source)) ||
        (vscode.workspace.workspaceFolders || [])[0];
    const configuration = vscode.workspace.getConfiguration('absolute', folder?.uri);
    let input = source;
    if (path.extname(source).toLowerCase() === '.abs' && configuration.get('preferProject', true))
        input = await nearestProject(source, folder) || source;

    let project;
    if (path.extname(input).toLowerCase() === '.absproj') {
        try { project = readJson(input); }
        catch (error) {
            if (forceWasm) vscode.window.showErrorMessage(`Invalid Absolute project: ${error.message || error}`);
            return undefined;
        }
    }

    const target = forceWasm
        ? (wasmTarget.isWasmTarget(project?.target) ? wasmTarget.projectTarget(project) : 'wasm32-unknown-unknown')
        : wasmTarget.projectTarget(project);
    if (!forceWasm && !wasmTarget.isWasmTarget(target)) return { handled: false };
    if (!wasmTarget.isWasmTarget(target)) return undefined;

    const projectType = String(project?.type || 'app').toLowerCase();
    if (projectType === 'lib' || projectType === 'library') {
        vscode.window.showErrorMessage('Absolute: WebAssembly Run requires an application project, not a library.');
        return { handled: true };
    }

    const projectRoot = project ? path.dirname(input) : path.dirname(source);
    const workspace = folder ? folder.uri.fsPath : projectRoot;
    const projectName = String(project?.name || path.basename(input, path.extname(input)));
    const configuredOutput = String(configuration.get('outputDirectory', '') || '').trim();
    const outputDirectory = configuredOutput
        ? resolveConfiguredPath(configuredOutput, workspace, folder, source)
        : (project ? path.join(projectRoot, 'build') : path.join(path.dirname(source), '.absolute', 'out'));
    fs.mkdirSync(outputDirectory, { recursive: true });
    const artifact = path.join(outputDirectory, wasmTarget.outputName(projectName, target));

    const roots = contextRoots(folder, source);
    const configuredCompiler = expandVariables(configuration.get('compilerPath', 'auto'), folder, source);
    const compilerResolution = toolchain.resolveCompilerPath(configuredCompiler, roots);
    if (!compilerResolution.path) {
        const details = compilerResolution.tried.length
            ? `\nChecked:\n${compilerResolution.tried.map(item => `  ${item}`).join('\n')}`
            : '';
        vscode.window.showErrorMessage(`Absolute compiler was not found.${details}`);
        return { handled: true };
    }

    const configuredPlugins = configuration.get('plugins', [])
        .map(item => resolveConfiguredPath(item, workspace, folder, source));
    if (project && Array.isArray(project.plugins)) {
        for (const item of project.plugins)
            configuredPlugins.push(resolveConfiguredPath(item, projectRoot, folder, source));
    }
    const discovered = toolchain.discoverPlugins(roots);
    const sourceForPlugins = project?.entry
        ? path.resolve(projectRoot, String(project.entry))
        : source;
    const selectedPlugins = toolchain.selectPluginsForSource(
        sourceForPlugins, discovered, configuredPlugins);
    const pluginArguments = toolchain.pluginArguments(selectedPlugins);
    const searchPaths = configuration.get('pluginSearchPaths', [])
        .map(item => resolveConfiguredPath(item, workspace, folder, source));
    if (project && Array.isArray(project.pluginSearchPaths)) {
        for (const item of project.pluginSearchPaths)
            searchPaths.push(resolveConfiguredPath(item, projectRoot, folder, source));
    }
    for (const searchPath of searchPaths) pluginArguments.push('--plugin-path', searchPath);

    const compilerArguments = path.extname(input).toLowerCase() === '.absproj'
        ? ['build', input]
        : [input];
    compilerArguments.push(
        ...configuration.get('compilerArguments', []).map(value => String(value)),
        ...pluginArguments,
        '--target', target,
        '--build-exe', '-o', artifact
    );

    const runArguments = [
        ...wasmTarget.projectRunArgs(project),
        ...configuration.get('runArguments', []).map(value => String(value))
    ].map(value => expandVariables(value, folder, source));

    return {
        handled: true,
        source,
        input,
        folder,
        workspace,
        configuration,
        target,
        runtime: wasmTarget.projectRuntime(project),
        artifact,
        compiler: compilerResolution.path,
        compilerArguments,
        runArguments,
        roots
    };
}

async function executeTaskAndWait(task) {
    let execution;
    const completion = new Promise(resolve => {
        const subscription = vscode.tasks.onDidEndTaskProcess(event => {
            if (event.execution === execution) {
                subscription.dispose();
                resolve(event.exitCode);
            }
        });
    });
    execution = await vscode.tasks.executeTask(task);
    return completion;
}

function taskScope(folder) {
    return folder || vscode.TaskScope.Workspace;
}

async function buildAndRunWasm(requested, forceWasm = false, debugRequested = false) {
    const build = await describeWasmBuild(requested, forceWasm);
    if (!build) {
        if (forceWasm) vscode.window.showErrorMessage('Absolute: open an .abs file or .absproj to run as WebAssembly.');
        return false;
    }
    if (!build.handled) return false;
    if (!build.compiler) return true;

    if (debugRequested) {
        vscode.window.showWarningMessage(
            'Absolute: native WASM debugging is not configured yet; building and running through the selected host.');
    }

    const buildExecution = new vscode.ProcessExecution(
        build.compiler,
        build.compilerArguments,
        { cwd: build.workspace, env: toolchain.nativeBuildEnvironment() }
    );
    const buildTask = new vscode.Task(
        { type: 'absolute-wasm-build', input: build.input },
        taskScope(build.folder),
        `Build ${path.basename(build.artifact)}`,
        'Absolute',
        buildExecution,
        []
    );
    buildTask.presentationOptions = {
        reveal: vscode.TaskRevealKind.Always,
        panel: vscode.TaskPanelKind.Dedicated,
        clear: true
    };
    const buildExit = await executeTaskAndWait(buildTask);
    if (buildExit !== 0 || !fs.existsSync(build.artifact)) {
        vscode.window.showErrorMessage(
            `Absolute WebAssembly build failed${buildExit == null ? '' : ` with exit code ${buildExit}`}.`);
        return true;
    }

    if (build.runtime === 'browser') {
        vscode.window.showInformationMessage(`Absolute WebAssembly module built: ${build.artifact}`);
        return true;
    }
    if (build.runtime !== 'node') {
        vscode.window.showErrorMessage(
            `Absolute: unsupported WebAssembly runtime '${build.runtime}'. Supported extension runtime: node.`);
        return true;
    }
    if (build.target.toLowerCase().includes('wasi')) {
        vscode.window.showErrorMessage(
            'Absolute: wasm32-wasi requires a WASI runner; use wasm32-unknown-unknown with runtime "node" for the extension host.');
        return true;
    }

    const host = wasmTarget.resolveWasmHost(build.roots, __dirname);
    if (!host) {
        vscode.window.showErrorMessage(
            'Absolute WASM host was not found. Repackage the extension or keep tools/absolute-wasm-host.js in the workspace.');
        return true;
    }
    const runner = path.join(__dirname, 'tools', 'absolute-wasm-run.js');
    const node = String(build.configuration.get('nodePath', 'node') || 'node');
    const runExecution = new vscode.ProcessExecution(
        node,
        [runner, host, build.artifact, ...build.runArguments],
        { cwd: build.workspace }
    );
    const runTask = new vscode.Task(
        { type: 'absolute-wasm-run', input: build.input },
        taskScope(build.folder),
        `Run ${path.basename(build.artifact)}`,
        'Absolute',
        runExecution,
        []
    );
    runTask.presentationOptions = {
        reveal: vscode.TaskRevealKind.Always,
        panel: vscode.TaskPanelKind.Dedicated
    };
    await vscode.tasks.executeTask(runTask);
    return true;
}

async function activate(context) {
    const captured = new Map();
    const commands = vscode.commands;
    const originalRegister = commands.registerCommand;
    commands.registerCommand = function registerAbsoluteCommand(command, callback, thisArg) {
        if (interceptedCommands.has(command)) {
            captured.set(command, { callback, thisArg });
            return { dispose() { } };
        }
        return originalRegister.call(commands, command, callback, thisArg);
    };

    try {
        await baseExtension.activate(context);
    } finally {
        commands.registerCommand = originalRegister;
    }

    const invokeOriginal = async (command, args) => {
        const registration = captured.get(command);
        if (!registration) return undefined;
        return registration.callback.apply(registration.thisArg, args);
    };

    const registerWrapped = (command, debugRequested = false) => {
        const disposable = originalRegister.call(commands, command, async (...args) => {
            const handled = await buildAndRunWasm(args[0], false, debugRequested);
            if (!handled) return invokeOriginal(command, args);
            return undefined;
        });
        context.subscriptions.push(disposable);
    };

    registerWrapped('absolute.runFile', false);
    registerWrapped('absolute.debugFile', true);
    registerWrapped('absolute.runProject', false);
    registerWrapped('absolute.debugProject', true);
    context.subscriptions.push(originalRegister.call(
        commands,
        'absolute.runFileWasm',
        resource => buildAndRunWasm(resource, true, false)
    ));
}

function deactivate() {
    return baseExtension.deactivate ? baseExtension.deactivate() : undefined;
}

module.exports = { activate, deactivate };
