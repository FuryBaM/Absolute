#!/usr/bin/env node
'use strict';

/**
 * Absolute developer CLI:
 *   node tools/absolute-dev.js fmt [files...]
 *   node tools/absolute-dev.js test [ctest-args...]
 *   node tools/absolute-dev.js doc [roots...] [-o out.md]
 *   node tools/absolute-dev.js package list|resolve [project.absproj]
 *   node tools/absolute-dev.js eval <expression>
 *   node tools/absolute-dev.js repl
 *   node tools/absolute-dev.js bindgen <header.h> [bindgen-args...]
 *   node tools/absolute-dev.js wasm build|run|test ...
 */

const fs = require('fs');
const path = require('path');
const childProcess = require('child_process');

const repoRoot = path.resolve(__dirname, '..');
const language = require(path.join(repoRoot, 'absolute-extension', 'server', 'language.js'));
const repl = require(path.join(repoRoot, 'tools', 'absolute-repl.js'));

function usage() {
    console.log(`Usage:
  absolute-dev fmt [file.abs ...]
  absolute-dev test [-- [ctest args...]]
  absolute-dev doc [root ...] [-o out.md]
  absolute-dev package list <project.absproj>
  absolute-dev package resolve <project.absproj>
  absolute-dev eval <expression>
  absolute-dev repl
  absolute-dev bindgen <header.h> [-o out.abs] [-I dir] [--clang path]
  absolute-dev wasm build <file.abs> [-o out.wasm] [--runtime host|wasi|shared]
  absolute-dev wasm run <file.wasm> [--wasi] [--export name] [--args a,b] [--env K=V]
  absolute-dev wasm test [ctest-args...]
`);
}

function findAbsolutec() {
    if (process.env.ABSOLUTEC && fs.existsSync(process.env.ABSOLUTEC)) {
        return process.env.ABSOLUTEC;
    }
    const candidates = [
        path.join(repoRoot, '.absolute', 'build', 'windows-release', 'Release', 'absolutec.exe'),
        path.join(repoRoot, '.absolute', 'build', 'windows-release', 'absolutec.exe'),
        path.join(repoRoot, 'build', 'Release', 'absolutec.exe'),
        path.join(repoRoot, 'build', 'absolutec'),
        path.join(repoRoot, 'x64', 'Release', 'absolutec.exe'),
        path.join(repoRoot, 'x64', 'Debug', 'absolutec.exe'),
    ];
    for (const candidate of candidates) {
        if (fs.existsSync(candidate)) return candidate;
    }
    return 'absolutec';
}

function findWasmRuntimeObject(kind) {
    const names = {
        host: 'absolute_wasm_runtime.o',
        wasi: 'absolute_wasm_runtime_wasi.o',
        shared: 'absolute_wasm_runtime_shared.o',
    };
    const file = names[kind] || names.host;
    const roots = [
        path.join(repoRoot, '.absolute', 'build', 'windows-release'),
        path.join(repoRoot, 'build'),
        path.join(repoRoot, '.absolute', 'build', 'msvc-release'),
    ];
    for (const root of roots) {
        const full = path.join(root, file);
        if (fs.existsSync(full)) return full;
    }
    return null;
}

function findWasmLd() {
    if (process.env.ABSOLUTE_WASM_LD && fs.existsSync(process.env.ABSOLUTE_WASM_LD)) {
        return process.env.ABSOLUTE_WASM_LD;
    }
    const candidates = [
        path.join(repoRoot, '.absolute', 'toolchains', 'llvm-18.1.8', 'bin', 'wasm-ld.exe'),
        path.join(repoRoot, '.absolute', 'toolchains', 'llvm-18.1.8', 'bin', 'wasm-ld'),
    ];
    for (const candidate of candidates) {
        if (fs.existsSync(candidate)) return candidate;
    }
    return process.platform === 'win32' ? 'wasm-ld.exe' : 'wasm-ld';
}

function runWasmBuild(args) {
    let source = null;
    let output = null;
    let runtime = 'host';
    for (let i = 0; i < args.length; i += 1) {
        const arg = args[i];
        if (arg === '-o' || arg === '--output') {
            output = args[++i];
            continue;
        }
        if (arg === '--runtime') {
            runtime = String(args[++i] || 'host').toLowerCase();
            continue;
        }
        if (arg.startsWith('-')) {
            console.error(`unknown wasm build option: ${arg}`);
            return 1;
        }
        if (source) {
            console.error('wasm build accepts a single .abs source');
            return 1;
        }
        source = arg;
    }
    if (!source || !fs.existsSync(source)) {
        console.error('wasm build requires an existing .abs file');
        return 1;
    }
    if (!['host', 'wasi', 'shared'].includes(runtime)) {
        console.error('--runtime must be host, wasi, or shared');
        return 1;
    }
    const absolutec = findAbsolutec();
    const outWasm = path.resolve(output || path.basename(source, path.extname(source)) + '.wasm');
    const env = { ...process.env };
    if (runtime === 'wasi') env.ABSOLUTE_WASM_RUNTIME = 'wasi';
    if (runtime === 'shared') env.ABSOLUTE_WASM_RUNTIME = 'shared';

    // Prefer absolutec --build-exe when the matching runtime object is baked into absolutec.
    // Fallback: emit-object + wasm-ld with the runtime .o from the build tree.
    const preferDirect = runtime === 'host' || process.env.ABSOLUTE_WASM_FORCE_DIRECT === '1';
    if (preferDirect || runtime === 'wasi' || runtime === 'shared') {
        // Try direct first when absolutec knows about the runtime objects.
        const direct = childProcess.spawnSync(
            absolutec,
            [path.resolve(source), '--target', 'wasm32-unknown-unknown', '--build-exe', '-o', outWasm],
            { stdio: 'inherit', shell: true, env },
        );
        if (direct.status === 0 && fs.existsSync(outWasm)) {
            console.log(`wasm: built ${outWasm} (runtime=${runtime})`);
            return 0;
        }
        if (runtime === 'host') {
            return direct.status === null ? 1 : direct.status;
        }
        console.warn('wasm: direct --build-exe failed or incomplete; trying emit-object + wasm-ld');
    }

    const objectPath = outWasm + '.o';
    const emit = childProcess.spawnSync(
        absolutec,
        [path.resolve(source), '--target', 'wasm32-unknown-unknown', '--emit-object', '-o', objectPath],
        { stdio: 'inherit', shell: true, env },
    );
    if (emit.status !== 0) return emit.status === null ? 1 : emit.status;

    const runtimeObject = findWasmRuntimeObject(runtime);
    if (!runtimeObject) {
        console.error(`wasm runtime object for '${runtime}' not found; build Absolute (Absolute-Runtime-WasmShim) first`);
        return 1;
    }
    const wasmLd = findWasmLd();
    const linkArgs = ['--no-entry', '--export-all'];
    if (runtime === 'shared') {
        linkArgs.push('--shared-memory', '--import-memory', '--max-memory=16777216');
    }
    linkArgs.push(objectPath, runtimeObject, '-o', outWasm);
    console.log(`${wasmLd} ${linkArgs.join(' ')}`);
    const link = childProcess.spawnSync(wasmLd, linkArgs, { stdio: 'inherit', shell: true });
    if (link.status === 0) console.log(`wasm: built ${outWasm} (runtime=${runtime})`);
    return link.status === null ? 1 : link.status;
}

function runWasmRun(args) {
    let modulePath = null;
    let useWasi = false;
    let exportName = 'main';
    let callArgs = [];
    const envPairs = [];
    for (let i = 0; i < args.length; i += 1) {
        const arg = args[i];
        if (arg === '--wasi') {
            useWasi = true;
            continue;
        }
        if (arg === '--export') {
            exportName = args[++i] || 'main';
            continue;
        }
        if (arg === '--args') {
            const raw = args[++i] || '';
            callArgs = raw.split(',').filter(Boolean).map((part) => {
                if (/^-?\d+$/.test(part)) return Number(part);
                return part;
            });
            continue;
        }
        if (arg === '--env') {
            envPairs.push(args[++i] || '');
            continue;
        }
        if (arg.startsWith('-')) {
            console.error(`unknown wasm run option: ${arg}`);
            return 1;
        }
        if (modulePath) {
            console.error('wasm run accepts a single .wasm module');
            return 1;
        }
        modulePath = arg;
    }
    if (!modulePath || !fs.existsSync(modulePath)) {
        console.error('wasm run requires an existing .wasm file');
        return 1;
    }
    const absModule = path.resolve(modulePath);
    if (useWasi) {
        const runner = path.join(repoRoot, 'tools', 'absolute-wasm-wasi-run.js');
        const runnerArgs = [runner, absModule, '--export', exportName];
        for (const pair of envPairs) {
            if (pair) runnerArgs.push('--env', pair);
        }
        for (const value of callArgs) {
            runnerArgs.push('--arg', String(value));
        }
        const result = childProcess.spawnSync(process.execPath, runnerArgs, {
            stdio: 'inherit',
            env: { ...process.env, NODE_NO_WARNINGS: '1' },
        });
        return result.status === null ? 1 : result.status;
    }
    const runner = path.join(repoRoot, 'tools', 'absolute-wasm-run.js');
    const runnerArgs = [runner, absModule, '--export', exportName];
    if (callArgs.length) runnerArgs.push('--args', callArgs.join(','));
    const result = childProcess.spawnSync(process.execPath, runnerArgs, { stdio: 'inherit' });
    return result.status === null ? 1 : result.status;
}

function runWasmTest(args) {
    const filtered = args[0] === '--' ? args.slice(1) : args;
    return runTests(['-R', 'wasm', ...filtered]);
}

function runWasm(args) {
    const sub = args[0];
    const rest = args.slice(1);
    if (!sub || sub === '-h' || sub === '--help') {
        usage();
        return 0;
    }
    if (sub === 'build') return runWasmBuild(rest);
    if (sub === 'run') return runWasmRun(rest);
    if (sub === 'test') return runWasmTest(rest);
    console.error(`unknown wasm subcommand: ${sub}`);
    usage();
    return 1;
}

function formatFiles(files) {
    const targets = files.length ? files : language.walkAbsFiles(process.cwd());
    let changed = 0;
    for (const file of targets) {
        const abs = path.resolve(file);
        if (!abs.endsWith('.abs') || !fs.existsSync(abs)) continue;
        const original = fs.readFileSync(abs, 'utf8');
        const formatted = language.formatDocument(original);
        if (formatted !== original) {
            fs.writeFileSync(abs, formatted, 'utf8');
            console.log(`formatted ${abs}`);
            changed += 1;
        }
    }
    console.log(`fmt: ${changed} file(s) updated, ${targets.length} scanned`);
    return 0;
}

function runTests(args) {
    const buildDirs = [
        path.join(repoRoot, '.absolute', 'build', 'windows-release'),
        path.join(repoRoot, '.absolute', 'build', 'msvc-release'),
        path.join(repoRoot, 'build')
    ];
    const buildDir = buildDirs.find(dir => fs.existsSync(path.join(dir, 'CTestTestfile.cmake')));
    if (!buildDir) {
        console.error('No CTest build directory found. Configure/build Absolute first.');
        return 1;
    }
    const ctestArgs = ['--test-dir', buildDir, '--output-on-failure', ...args];
    console.log(`ctest ${ctestArgs.join(' ')}`);
    const result = childProcess.spawnSync('ctest', ctestArgs, { stdio: 'inherit', shell: true });
    return result.status === null ? 1 : result.status;
}

function runDoc(args) {
    let output = path.join(process.cwd(), 'absolute-api.md');
    const roots = [];
    for (let i = 0; i < args.length; ++i) {
        if (args[i] === '-o' || args[i] === '--output') {
            output = path.resolve(args[++i] || output);
            continue;
        }
        roots.push(path.resolve(args[i]));
    }
    if (!roots.length) roots.push(path.join(repoRoot, 'std'), path.join(repoRoot, 'tests'));
    const markdown = language.generateDocMarkdown(roots);
    fs.writeFileSync(output, markdown, 'utf8');
    console.log(`wrote ${output}`);
    return 0;
}

function packageList(projectFile) {
    if (!projectFile || !fs.existsSync(projectFile)) {
        console.error('package list requires an existing .absproj');
        return 1;
    }
    const project = JSON.parse(fs.readFileSync(projectFile, 'utf8').replace(/^\uFEFF/, ''));
    console.log(`project: ${project.name || path.basename(projectFile)}`);
    console.log(`entry: ${project.entry || '?'}`);
    const deps = project.dependencies || project.packages || {};
    if (Array.isArray(deps)) {
        for (const dep of deps) console.log(`- ${dep.name || dep}${dep.version ? `@${dep.version}` : ''}`);
    } else {
        for (const [name, version] of Object.entries(deps)) console.log(`- ${name}@${version}`);
    }
    if (Array.isArray(project.plugins)) {
        console.log('plugins:');
        for (const plugin of project.plugins) console.log(`- ${plugin}`);
    }
    return 0;
}

function packageResolve(projectFile) {
    // Thin wrapper: use absolutec package manager if available through build project dry-run.
    if (!projectFile || !fs.existsSync(projectFile)) {
        console.error('package resolve requires an existing .absproj');
        return 1;
    }
    const absolutec = process.env.ABSOLUTEC || 'absolutec';
    const result = childProcess.spawnSync(absolutec, ['build', projectFile, '--parse-only'], {
        encoding: 'utf8',
        shell: true
    });
    process.stdout.write(result.stdout || '');
    process.stderr.write(result.stderr || '');
    if (result.status === 0) console.log(`package resolve: ${projectFile} OK`);
    return result.status === null ? 1 : result.status;
}

function main(argv) {
    const [command, ...rest] = argv;
    if (!command || command === '-h' || command === '--help') {
        usage();
        return 0;
    }
    switch (command) {
    case 'fmt':
    case 'format':
        return formatFiles(rest);
    case 'test':
        return runTests(rest[0] === '--' ? rest.slice(1) : rest);
    case 'doc':
        return runDoc(rest);
    case 'package': {
        const sub = rest[0];
        if (sub === 'list') return packageList(rest[1]);
        if (sub === 'resolve') return packageResolve(rest[1]);
        usage();
        return 1;
    }
    case 'eval':
    case 'evaluate': {
        const expression = rest.join(' ');
        if (!expression.trim()) {
            console.error('eval requires an expression');
            return 1;
        }
        const result = repl.evaluate(expression);
        if (result.stdout) process.stdout.write(result.stdout);
        if (result.stderr) process.stderr.write(result.stderr);
        return result.ok ? 0 : 1;
    }
    case 'repl':
        repl.startRepl();
        return 0;
    case 'bindgen': {
        const bindgen = require(path.join(repoRoot, 'tools', 'absolute-bindgen.js'));
        return bindgen.main(rest);
    }
    case 'wasm':
        return runWasm(rest);
    default:
        console.error(`unknown command: ${command}`);
        usage();
        return 1;
    }
}

process.exit(main(process.argv.slice(2)));
