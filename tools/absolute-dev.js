#!/usr/bin/env node
'use strict';

/**
 * Absolute developer CLI:
 *   node tools/absolute-dev.js build-compiler [build-windows args...]
 *   node tools/absolute-dev.js compiler [absolutec args...]
 *   node tools/absolute-dev.js compile <file.abs|project.absproj> [compiler args...]
 *   node tools/absolute-dev.js run <file.abs|project.absproj> [compiler args...] [-- program args...]
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
  absolute build-compiler [--bootstrap] [--frontend] [-Clean] [--test]
  absolute compiler [absolutec arguments...]
  absolute compile <file.abs|project.absproj> [compiler arguments...]
  absolute run <file.abs|project.absproj> [compiler arguments...] [-- program arguments...]
  absolute fmt [file.abs ...]
  absolute test [-- [ctest args...]]
  absolute doc [root ...] [-o out.md]
  absolute package list <project.absproj>
  absolute package resolve <project.absproj>
  absolute eval <expression>
  absolute repl
  absolute bindgen <header.h> [-o out.abs] [-I dir] [--clang path]
  absolute wasm build <file.abs> [-o out.wasm] [--runtime host|wasi|shared]
  absolute wasm run <file.wasm> [--wasi] [--export name] [--args a,b] [--env K=V]
  absolute wasm test [ctest-args...]

Examples:
  absolute build-compiler --bootstrap
  absolute compile hello.abs
  absolute run hello.abs
  absolute run hello.abs -- first-argument "second argument"
`);
}

function findAbsolutec() {
    if (process.env.ABSOLUTEC && fs.existsSync(process.env.ABSOLUTEC)) {
        return path.resolve(process.env.ABSOLUTEC);
    }
    const candidates = [
        path.join(repoRoot, '.absolute', 'build', 'windows-release', 'Release', 'absolutec.exe'),
        path.join(repoRoot, '.absolute', 'build', 'windows-release', 'absolutec.exe'),
        path.join(repoRoot, '.absolute', 'build', 'windows-clang-cl-release', 'Release', 'absolutec.exe'),
        path.join(repoRoot, '.absolute', 'build', 'windows-clang-cl-release', 'absolutec.exe'),
        path.join(repoRoot, '.absolute', 'build', 'windows-release-sccache', 'Release', 'absolutec.exe'),
        path.join(repoRoot, '.absolute', 'build', 'windows-release-sccache', 'absolutec.exe'),
        path.join(repoRoot, '.absolute', 'build', 'windows-frontend', 'Release', 'absolutec.exe'),
        path.join(repoRoot, '.absolute', 'build', 'windows-frontend', 'absolutec.exe'),
        path.join(repoRoot, '.absolute', 'build', 'windows-clang-cl-frontend', 'Release', 'absolutec.exe'),
        path.join(repoRoot, '.absolute', 'build', 'windows-clang-cl-frontend', 'absolutec.exe'),
        path.join(repoRoot, 'build', 'Release', 'absolutec.exe'),
        path.join(repoRoot, 'build', 'Debug', 'absolutec.exe'),
        path.join(repoRoot, 'build', 'absolutec'),
        path.join(repoRoot, 'x64', 'Release', 'absolutec.exe'),
        path.join(repoRoot, 'x64', 'Debug', 'absolutec.exe'),
    ];
    for (const candidate of candidates) {
        if (fs.existsSync(candidate)) return candidate;
    }
    return 'absolutec';
}

function compilerWasFound(compiler) {
    if (path.isAbsolute(compiler)) return fs.existsSync(compiler);
    const lookup = childProcess.spawnSync(
        process.platform === 'win32' ? 'where.exe' : 'which',
        [compiler],
        { stdio: 'ignore' },
    );
    return lookup.status === 0;
}

let cachedCompilerEnvironment = null;

function findVisualStudioEnvironment() {
    if (process.platform !== 'win32') return process.env;
    if (cachedCompilerEnvironment) return cachedCompilerEnvironment;
    if (process.env.VSCMD_ARG_TGT_ARCH && process.env.LIB) {
        cachedCompilerEnvironment = process.env;
        return cachedCompilerEnvironment;
    }

    const programFilesX86 = process.env['ProgramFiles(x86)'];
    const vswhere = programFilesX86 &&
        path.join(programFilesX86, 'Microsoft Visual Studio', 'Installer', 'vswhere.exe');
    if (!vswhere || !fs.existsSync(vswhere)) {
        cachedCompilerEnvironment = process.env;
        return cachedCompilerEnvironment;
    }

    const locate = childProcess.spawnSync(vswhere, [
        '-latest',
        '-products', '*',
        '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
        '-property', 'installationPath',
    ], { encoding: 'utf8' });
    const installPath = String(locate.stdout || '').trim();
    const vcvars = path.join(installPath, 'VC', 'Auxiliary', 'Build', 'vcvars64.bat');
    if (locate.status !== 0 || !installPath || !fs.existsSync(vcvars)) {
        cachedCompilerEnvironment = process.env;
        return cachedCompilerEnvironment;
    }

    const comspec = process.env.ComSpec || 'cmd.exe';
    const initialized = childProcess.spawnSync(
        comspec,
        ['/d', '/s', '/c', `""${vcvars}" >nul && set"`],
        {
            encoding: 'utf8',
            env: process.env,
            windowsVerbatimArguments: true,
        },
    );
    if (initialized.status !== 0) {
        cachedCompilerEnvironment = process.env;
        return cachedCompilerEnvironment;
    }

    const environment = { ...process.env };
    for (const line of String(initialized.stdout || '').split(/\r?\n/)) {
        const separator = line.indexOf('=');
        if (separator <= 0) continue;
        const name = line.slice(0, separator);
        const duplicate = Object.keys(environment).find(
            key => key !== name && key.toLowerCase() === name.toLowerCase(),
        );
        if (duplicate) delete environment[duplicate];
        environment[name] = line.slice(separator + 1);
    }
    cachedCompilerEnvironment = environment;
    return cachedCompilerEnvironment;
}

function runCompiler(args) {
    const compiler = findAbsolutec();
    if (!compilerWasFound(compiler)) {
        console.error('Absolute compiler was not found.');
        console.error('Build it first with: absolute build-compiler --bootstrap');
        console.error('Or set ABSOLUTEC to the full path of the compiler executable.');
        return 1;
    }
    const result = childProcess.spawnSync(compiler, args, {
        stdio: 'inherit',
        env: findVisualStudioEnvironment(),
    });
    if (result.error) {
        console.error(`failed to start Absolute compiler: ${result.error.message}`);
        return 1;
    }
    return result.status === null ? 1 : result.status;
}

function inputCompilerArguments(input) {
    const absoluteInput = path.resolve(input);
    return path.extname(input).toLowerCase() === '.absproj'
        ? ['build', absoluteInput]
        : [absoluteInput];
}

function normalizeOutputArguments(args) {
    const normalized = [];
    for (let index = 0; index < args.length; index += 1) {
        const argument = args[index];
        if (argument === '--output') {
            normalized.push('-o');
            if (index + 1 < args.length) normalized.push(args[++index]);
            continue;
        }
        if (argument.startsWith('--output=')) {
            normalized.push('-o', argument.slice('--output='.length));
            continue;
        }
        normalized.push(argument);
    }
    return normalized;
}

function hasOutputMode(args) {
    return args.some(argument =>
        argument === '--parse-only' ||
        argument === '--emit-llvm' ||
        argument === '--emit-object' ||
        argument === '--build-exe');
}

function runCompile(args) {
    if (!args.length || args[0] === '-h' || args[0] === '--help') {
        console.log('Usage: absolute compile <file.abs|project.absproj> [compiler arguments...]');
        console.log('Defaults to --build-exe. Use --output <path> as an alias for -o.');
        return args.length ? 0 : 1;
    }
    const [input, ...rawOptions] = args;
    if (!fs.existsSync(input)) {
        console.error(`input file was not found: ${input}`);
        return 1;
    }
    const options = normalizeOutputArguments(rawOptions);
    const output = outputFromArguments(options);
    if (options.includes('-o') && !output) {
        console.error('-o/--output requires a path');
        return 1;
    }
    if (output) fs.mkdirSync(path.dirname(path.resolve(output)), { recursive: true });
    if (!hasOutputMode(options)) options.push('--build-exe');
    return runCompiler([...inputCompilerArguments(input), ...options]);
}

function outputFromArguments(args) {
    for (let index = 0; index < args.length; index += 1) {
        if (args[index] === '-o') return args[index + 1] || null;
    }
    return null;
}

function defaultRunOutput(input) {
    const name = path.basename(input, path.extname(input));
    const extension = process.platform === 'win32' ? '.exe' : '';
    return path.join(repoRoot, '.absolute', 'run', name + extension);
}

function runNative(args) {
    if (!args.length || args[0] === '-h' || args[0] === '--help') {
        console.log('Usage: absolute run <file.abs|project.absproj> [compiler arguments...] [-- program arguments...]');
        console.log('The first -- separates compiler arguments from arguments passed to the program.');
        return args.length ? 0 : 1;
    }

    const separator = args.indexOf('--');
    const compileArgs = separator === -1 ? args : args.slice(0, separator);
    const programArgs = separator === -1 ? [] : args.slice(separator + 1);
    const [input, ...rawOptions] = compileArgs;
    if (!input || !fs.existsSync(input)) {
        console.error(`input file was not found: ${input || '(missing)'}`);
        return 1;
    }
    const options = normalizeOutputArguments(rawOptions);
    if (hasOutputMode(options)) {
        console.error('absolute run selects --build-exe automatically; remove the explicit output mode');
        return 1;
    }
    if (options.some(argument => argument === '--target' || argument.startsWith('--target='))) {
        console.error('absolute run supports native executables only; use absolute compile for another target');
        return 1;
    }

    let output = outputFromArguments(options);
    if (!output) {
        output = defaultRunOutput(input);
        fs.mkdirSync(path.dirname(output), { recursive: true });
        options.push('-o', output);
    }
    else {
        output = path.resolve(output);
        fs.mkdirSync(path.dirname(output), { recursive: true });
    }

    const compileStatus = runCompiler([
        ...inputCompilerArguments(input),
        ...options,
        '--build-exe',
    ]);
    if (compileStatus !== 0) return compileStatus;
    if (!fs.existsSync(output)) {
        console.error(`compiler finished without creating the executable: ${output}`);
        return 1;
    }

    console.log(`Running ${output}${programArgs.length ? ` ${programArgs.join(' ')}` : ''}`);
    const result = childProcess.spawnSync(output, programArgs, { stdio: 'inherit' });
    if (result.error) {
        console.error(`failed to start program: ${result.error.message}`);
        return 1;
    }
    return result.status === null ? 1 : result.status;
}

function buildCompiler(args) {
    const withTests = args.includes('--test');
    const buildArgs = args.filter(argument =>
        argument !== '--test' &&
        !(withTests && argument.toLowerCase() === '-notest'));
    if (process.platform === 'win32') {
        const script = path.join(repoRoot, 'build-windows.bat');
        if (
            !withTests &&
            !buildArgs.some(argument => argument.toLowerCase() === '-notest')
        ) {
            buildArgs.push('-NoTest');
        }
        const result = childProcess.spawnSync(script, buildArgs, {
            stdio: 'inherit',
            shell: true,
            cwd: repoRoot,
        });
        return result.status === null ? 1 : result.status;
    }

    if (buildArgs.length) {
        console.error('build-compiler arguments currently apply to the native Windows build');
        return 1;
    }
    let result = childProcess.spawnSync(
        'cmake',
        ['-S', repoRoot, '-B', path.join(repoRoot, 'build'), '-DCMAKE_BUILD_TYPE=Release'],
        { stdio: 'inherit' },
    );
    if (result.status !== 0) return result.status === null ? 1 : result.status;
    result = childProcess.spawnSync(
        'cmake',
        ['--build', path.join(repoRoot, 'build'), '--parallel'],
        { stdio: 'inherit' },
    );
    if (result.status !== 0) return result.status === null ? 1 : result.status;
    if (!withTests) return 0;
    result = childProcess.spawnSync(
        'ctest',
        ['--test-dir', path.join(repoRoot, 'build'), '--output-on-failure'],
        { stdio: 'inherit' },
    );
    return result.status === null ? 1 : result.status;
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
    const result = childProcess.spawnSync('ctest', ctestArgs, {
        stdio: 'inherit',
        env: findVisualStudioEnvironment(),
    });
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
    const status = runCompiler(['build', path.resolve(projectFile), '--parse-only']);
    if (status === 0) console.log(`package resolve: ${projectFile} OK`);
    return status;
}

function main(argv) {
    const [command, ...rest] = argv;
    if (!command || command === '-h' || command === '--help') {
        usage();
        return 0;
    }
    switch (command) {
    case 'build-compiler':
        return buildCompiler(rest);
    case 'bootstrap':
        return buildCompiler(['--bootstrap', ...rest]);
    case 'compiler':
    case 'cc':
        if (!rest.length) {
            console.error('compiler requires arguments; try: absolute compiler --help');
            return 1;
        }
        if (rest.length === 1 && ['-h', '--help', 'help'].includes(rest[0])) {
            console.log(`Usage:
  absolute compiler <source.abs> [options]
  absolute compiler build <project.absproj> [options]
  absolute compiler new <name> [--directory path]
Options:
  --parse-only | --emit-llvm | --emit-object | --build-exe
  --target <triple>
  --sanitize=address
  --plugin <path> | --plugin-path <directory> | -o <output>`);
            return 0;
        }
        return runCompiler(rest);
    case 'compile':
    case 'build':
        return runCompile(rest);
    case 'run':
        return runNative(rest);
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
        if (
            fs.existsSync(command) &&
            ['.abs', '.absproj'].includes(path.extname(command).toLowerCase())
        ) {
            return runCompile([command, ...rest]);
        }
        console.error(`unknown command: ${command}`);
        usage();
        return 1;
    }
}

process.exit(main(process.argv.slice(2)));
