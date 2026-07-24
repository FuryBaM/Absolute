'use strict';

/**
 * Absolute expression evaluator / REPL driver.
 * Compiles temporary programs with absolutec --build-exe and runs them.
 */

const fs = require('fs');
const path = require('path');
const os = require('os');
const childProcess = require('child_process');
const language = require(path.join(__dirname, '..', 'absolute-extension', 'server', 'language.js'));

function findCompiler(explicit) {
    if (explicit && fs.existsSync(explicit)) return explicit;
    if (process.env.ABSOLUTEC && fs.existsSync(process.env.ABSOLUTEC)) return process.env.ABSOLUTEC;
    const candidates = [
        path.join(__dirname, '..', '.absolute', 'build', 'windows-release', 'Release', 'absolutec.exe'),
        path.join(__dirname, '..', '.absolute', 'build', 'msvc-release', 'Release', 'absolutec.exe'),
        path.join(__dirname, '..', 'x64', 'Release', 'absolutec.exe'),
        'absolutec'
    ];
    for (const candidate of candidates) {
        if (candidate === 'absolutec') return candidate;
        if (fs.existsSync(candidate)) return candidate;
    }
    return 'absolutec';
}

function findVcvars64() {
    if (process.platform !== 'win32') return undefined;
    const vswhere = path.join(process.env['ProgramFiles(x86)'] || '',
        'Microsoft Visual Studio', 'Installer', 'vswhere.exe');
    if (!fs.existsSync(vswhere)) return undefined;
    const listed = childProcess.spawnSync(vswhere, [
        '-latest', '-products', '*',
        '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
        '-property', 'installationPath'
    ], { encoding: 'utf8', windowsHide: true });
    const install = (listed.stdout || '').trim().split(/\r?\n/).filter(Boolean).pop();
    if (!install) return undefined;
    const vcvars = path.join(install, 'VC', 'Auxiliary', 'Build', 'vcvars64.bat');
    return fs.existsSync(vcvars) ? vcvars : undefined;
}

function spawnWithMsvcEnv(command, args, options = {}) {
    if (process.platform !== 'win32') {
        return childProcess.spawnSync(command, args, options);
    }
    const vcvars = findVcvars64();
    if (!vcvars) return childProcess.spawnSync(command, args, options);
    // shell:true + "vcvars.bat" && tool — works with spaces in VS install path.
    const quoted = [command, ...args].map(part => {
        const text = String(part);
        return `"${text.replace(/"/g, '""')}"`;
    }).join(' ');
    const script = `"${vcvars}" && ${quoted}`;
    return childProcess.spawnSync(script, {
        ...options,
        shell: true,
        windowsHide: true
    });
}

function runSource(sourceText, options = {}) {
    const compiler = findCompiler(options.compilerPath);
    const work = fs.mkdtempSync(path.join(os.tmpdir(), 'absolute-repl-'));
    const sourceFile = path.join(work, 'eval.abs');
    const exe = path.join(work, process.platform === 'win32' ? 'eval.exe' : 'eval');
    fs.writeFileSync(sourceFile, sourceText, 'utf8');
    try {
        const build = spawnWithMsvcEnv(
            compiler,
            [sourceFile, '--build-exe', '-o', exe],
            { encoding: 'utf8', windowsHide: true, timeout: options.timeoutMs || 120000 }
        );
        if (build.status !== 0 || !fs.existsSync(exe)) {
            return {
                ok: false,
                stage: 'build',
                stdout: build.stdout || '',
                stderr: build.stderr || '',
                code: build.status
            };
        }
        const run = childProcess.spawnSync(exe, [], {
            encoding: 'utf8',
            windowsHide: true,
            timeout: options.timeoutMs || 15000
        });
        return {
            ok: run.status === 0,
            stage: 'run',
            stdout: run.stdout || '',
            stderr: run.stderr || '',
            code: run.status
        };
    } finally {
        try {
            for (const name of fs.readdirSync(work)) fs.unlinkSync(path.join(work, name));
            fs.rmdirSync(work);
        } catch (_) { /* ignore cleanup errors */ }
    }
}

function evaluate(expression, preamble = '', options = {}) {
    const source = language.evaluateExpressionSource(expression, preamble);
    return { source, ...runSource(source, options) };
}

function startRepl(options = {}) {
    const readline = require('readline');
    let preamble = options.preamble || '';
    const rl = readline.createInterface({
        input: process.stdin,
        output: process.stdout,
        prompt: 'abs> '
    });
    console.log('Absolute REPL. Commands: :quit :reset :preamble <code>  |  expressions/statements otherwise.');
    rl.prompt();
    rl.on('line', line => {
        const text = line.trim();
        if (!text) { rl.prompt(); return; }
        if (text === ':quit' || text === ':exit') { rl.close(); return; }
        if (text === ':reset') { preamble = ''; console.log('(preamble cleared)'); rl.prompt(); return; }
        if (text.startsWith(':preamble ')) {
            preamble += text.slice(':preamble '.length) + '\n';
            console.log('(preamble updated)');
            rl.prompt();
            return;
        }
        const result = evaluate(text, preamble, options);
        if (result.stdout) process.stdout.write(result.stdout);
        if (result.stderr) process.stderr.write(result.stderr);
        if (!result.ok) console.error(`[${result.stage} failed, code ${result.code}]`);
        // Persist successful top-level declarations into preamble (heuristic).
        if (result.ok && /^(class|struct|enum|namespace|interface|int32|int64|string|bool|void|async|func)\b/.test(text)) {
            preamble += text.endsWith(';') || text.endsWith('}') ? text + '\n' : text + ';\n';
        }
        rl.prompt();
    });
    rl.on('close', () => process.exit(0));
}

module.exports = { evaluate, runSource, findCompiler, startRepl };

if (require.main === module) {
    const args = process.argv.slice(2);
    if (args[0] === '-e' || args[0] === '--eval') {
        const result = evaluate(args.slice(1).join(' '));
        if (result.stdout) process.stdout.write(result.stdout);
        if (result.stderr) process.stderr.write(result.stderr);
        process.exit(result.ok ? 0 : 1);
    } else {
        startRepl();
    }
}
