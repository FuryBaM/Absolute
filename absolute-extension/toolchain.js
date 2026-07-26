'use strict';

const fs = require('fs');
const path = require('path');
const childProcess = require('child_process');

function uniqueExisting(values) {
    const seen = new Set();
    const result = [];
    for (const value of values) {
        if (!value) continue;
        const resolved = path.resolve(value);
        const key = process.platform === 'win32' ? resolved.toLowerCase() : resolved;
        if (seen.has(key) || !fs.existsSync(resolved)) continue;
        seen.add(key);
        result.push(resolved);
    }
    return result;
}

function candidateRoots(values = []) {
    const roots = [];
    for (const value of values) {
        if (!value) continue;
        let current = path.resolve(value);
        try {
            if (fs.existsSync(current) && fs.statSync(current).isFile()) current = path.dirname(current);
        } catch (_) { }
        for (let depth = 0; depth < 8; depth += 1) {
            roots.push(current);
            const parent = path.dirname(current);
            if (parent === current) break;
            current = parent;
        }
    }
    return uniqueExisting(roots);
}

function commandAvailable(command) {
    if (!command) return false;
    if (path.isAbsolute(command) || command.includes(path.sep))
        return fs.existsSync(path.resolve(command));
    const probe = childProcess.spawnSync(
        process.platform === 'win32' ? 'where.exe' : 'which',
        [command],
        { stdio: 'ignore', windowsHide: true }
    );
    return probe.status === 0;
}

let cachedNativeEnvironment;

function nativeBuildEnvironment(base = process.env) {
    if (process.platform !== 'win32') return base;
    if (cachedNativeEnvironment) return cachedNativeEnvironment;
    if (base.VSCMD_ARG_TGT_ARCH && base.LIB) {
        cachedNativeEnvironment = base;
        return cachedNativeEnvironment;
    }
    const programFilesX86 = base['ProgramFiles(x86)'];
    const vswhere = programFilesX86 &&
        path.join(programFilesX86, 'Microsoft Visual Studio', 'Installer', 'vswhere.exe');
    if (!vswhere || !fs.existsSync(vswhere)) return base;
    const locate = childProcess.spawnSync(vswhere, [
        '-latest', '-products', '*',
        '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
        '-property', 'installationPath'
    ], { encoding: 'utf8', windowsHide: true });
    const installation = String(locate.stdout || '').trim().split(/\r?\n/).filter(Boolean).pop();
    const vcvars = installation &&
        path.join(installation, 'VC', 'Auxiliary', 'Build', 'vcvars64.bat');
    if (locate.status !== 0 || !vcvars || !fs.existsSync(vcvars)) return base;
    const comspec = base.ComSpec || 'cmd.exe';
    const initialized = childProcess.spawnSync(
        comspec,
        ['/d', '/s', '/c', `""${vcvars}" >nul && set"`],
        {
            encoding: 'utf8',
            env: base,
            windowsHide: true,
            windowsVerbatimArguments: true
        }
    );
    if (initialized.status !== 0) return base;
    const environment = { ...base };
    for (const line of String(initialized.stdout || '').split(/\r?\n/)) {
        const separator = line.indexOf('=');
        if (separator <= 0) continue;
        const name = line.slice(0, separator);
        const duplicate = Object.keys(environment).find(
            key => key !== name && key.toLowerCase() === name.toLowerCase());
        if (duplicate) delete environment[duplicate];
        environment[name] = line.slice(separator + 1);
    }
    cachedNativeEnvironment = environment;
    return cachedNativeEnvironment;
}

function compilerCandidates(roots) {
    const relative = process.platform === 'win32'
        ? [
            ['.absolute', 'build', 'windows-release', 'Release', 'absolutec.exe'],
            ['.absolute', 'build', 'windows-release', 'absolutec.exe'],
            ['.absolute', 'build', 'windows-clang-cl-release', 'Release', 'absolutec.exe'],
            ['.absolute', 'build', 'windows-clang-cl-release', 'absolutec.exe'],
            ['.absolute', 'build', 'windows-release-sccache', 'Release', 'absolutec.exe'],
            ['.absolute', 'build', 'windows-release-sccache', 'absolutec.exe'],
            ['build', 'Release', 'absolutec.exe'],
            ['build', 'Debug', 'absolutec.exe'],
            ['x64', 'Release', 'absolutec.exe'],
            ['x64', 'Debug', 'absolutec.exe']
        ]
        : [
            ['.absolute', 'build', 'release', 'absolutec'],
            ['.absolute', 'build', 'linux-release', 'absolutec'],
            ['build', 'absolutec'],
            ['build', 'Release', 'absolutec']
        ];
    return roots.flatMap(root => relative.map(parts => path.join(root, ...parts)));
}

function configuredCompilerCandidates(value, roots) {
    const bases = path.isAbsolute(value)
        ? [path.normalize(value)]
        : roots.map(root => path.resolve(root, value));
    const names = process.platform === 'win32' ? ['absolutec.exe', 'absolutec'] : ['absolutec'];
    const result = [];
    for (const base of bases) {
        result.push(base);
        if (process.platform === 'win32' && !path.extname(base)) result.push(`${base}.exe`);
        try {
            if (fs.existsSync(base) && fs.statSync(base).isDirectory()) {
                for (const name of names) {
                    result.push(path.join(base, name));
                    result.push(path.join(base, 'Release', name));
                    result.push(path.join(base, 'Debug', name));
                }
            }
        } catch (_) { }
    }
    return result;
}

function resolveCompilerPath(configured, roots = []) {
    const value = String(configured || 'auto').trim();
    const lower = value.toLowerCase();
    const baseName = path.basename(value).toLowerCase();
    const compilerNameHint = baseName === 'absolutec' || baseName === 'absolutec.exe';
    const automatic = !value || lower === 'auto' || compilerNameHint;
    const tried = [];
    const seen = new Set();

    const tryCandidate = candidate => {
        if (!candidate) return undefined;
        const normalized = path.normalize(candidate);
        const key = process.platform === 'win32' ? normalized.toLowerCase() : normalized;
        if (seen.has(key)) return undefined;
        seen.add(key);
        tried.push(normalized);
        try {
            if (fs.existsSync(normalized) && fs.statSync(normalized).isFile())
                return path.resolve(normalized);
        } catch (_) { }
        return undefined;
    };

    if (lower !== 'auto') {
        for (const candidate of configuredCompilerCandidates(value, roots)) {
            const found = tryCandidate(candidate);
            if (found) return { path: found, tried, automatic };
        }
        if (!automatic) {
            if (commandAvailable(value)) return { path: value, tried, automatic: false };
            return { path: undefined, tried, automatic: false };
        }
    }

    if (process.env.ABSOLUTEC) {
        const found = tryCandidate(process.env.ABSOLUTEC);
        if (found) return { path: found, tried, automatic: true };
    }
    for (const candidate of compilerCandidates(roots)) {
        const found = tryCandidate(candidate);
        if (found) return { path: found, tried, automatic: true };
    }
    if (commandAvailable('absolutec.exe')) return { path: 'absolutec.exe', tried, automatic: true };
    if (commandAvailable('absolutec')) return { path: 'absolutec', tried, automatic: true };
    return { path: undefined, tried, automatic: true };
}

function buildDirectories(roots) {
    const result = [];
    for (const root of roots) {
        for (const parent of [path.join(root, '.absolute', 'build'), path.join(root, 'build')]) {
            if (!fs.existsSync(parent)) continue;
            result.push(parent);
            try {
                for (const entry of fs.readdirSync(parent, { withFileTypes: true })) {
                    if (entry.isDirectory()) result.push(path.join(parent, entry.name));
                }
            } catch (_) { }
        }
    }
    return uniqueExisting(result);
}

function firstFile(builds, variants) {
    for (const build of builds) {
        for (const parts of variants) {
            const candidate = path.join(build, ...parts);
            if (fs.existsSync(candidate)) return path.resolve(candidate);
        }
    }
    return undefined;
}

function discoverPlugins(roots = []) {
    const builds = buildDirectories(roots);
    const libraryExtension = process.platform === 'win32' ? '.dll' :
        (process.platform === 'darwin' ? '.dylib' : '.so');
    const desktop = firstFile(builds, [
        ['plugins', 'desktop', 'absolute-desktop.absplugin'],
        ['plugins', 'desktop', 'Release', 'absolute-desktop.absplugin']
    ]);
    const math = firstFile(builds, [
        ['plugins', 'math', `absolute-math${libraryExtension}`],
        ['plugins', 'math', 'Release', `absolute-math${libraryExtension}`]
    ]);
    const shader = firstFile(builds, [
        ['plugins', 'shader', `absolute-shader${libraryExtension}`],
        ['plugins', 'shader', 'Release', `absolute-shader${libraryExtension}`]
    ]);
    const metadata = uniqueExisting([
        math && path.join(path.dirname(math), 'absolute-math.editor.json'),
        shader && path.join(path.dirname(shader), 'absolute-shader.editor.json')
    ]);
    return {
        desktop,
        math,
        shader,
        all: uniqueExisting([desktop, math, shader]),
        metadata
    };
}

function sourceText(source) {
    if (!source || path.extname(source).toLowerCase() !== '.abs' || !fs.existsSync(source)) return '';
    try { return fs.readFileSync(source, 'utf8'); }
    catch (_) { return ''; }
}

function selectPluginsForSource(source, discovered, configured = []) {
    const text = sourceText(source);
    const selected = [...configured];
    const needsShader = /(?:^|\s)shader\s+[A-Za-z_][A-Za-z0-9_]*\s*\{|@shader[.]/m.test(text);
    const needsMath = /\bMath[.]/.test(text) || needsShader;
    const needsDesktop = /\bDesktop[.]/.test(text);
    if (needsMath && discovered && discovered.math) selected.push(discovered.math);
    if (needsShader && discovered && discovered.shader) selected.push(discovered.shader);
    if (needsDesktop && discovered && discovered.desktop) selected.push(discovered.desktop);
    return uniqueExisting(selected);
}

function pluginArguments(plugins) {
    return plugins.flatMap(plugin => ['--plugin', plugin]);
}

module.exports = {
    candidateRoots,
    commandAvailable,
    nativeBuildEnvironment,
    compilerCandidates,
    resolveCompilerPath,
    discoverPlugins,
    selectPluginsForSource,
    pluginArguments,
    uniqueExisting
};
