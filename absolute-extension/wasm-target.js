'use strict';

const fs = require('fs');
const path = require('path');

function normalizeTarget(value) {
    const target = String(value || '').trim();
    const lower = target.toLowerCase();
    if (!target || lower === 'host' || lower === 'native') return '';
    return target;
}

function isWasmTarget(value) {
    const target = normalizeTarget(value).toLowerCase();
    return target.startsWith('wasm32') || target.startsWith('wasm64') ||
        target.includes('wasm32') || target.includes('wasm64');
}

function projectTarget(project) {
    return normalizeTarget(project && project.target);
}

function projectRuntime(project) {
    const runtime = String(project && project.runtime || '').trim().toLowerCase();
    return runtime || 'node';
}

function projectRunArgs(project) {
    return Array.isArray(project && project.runArgs)
        ? project.runArgs.map(value => String(value))
        : [];
}

function uniqueExistingFiles(values) {
    const seen = new Set();
    const result = [];
    for (const value of values) {
        if (!value) continue;
        const resolved = path.resolve(value);
        const key = process.platform === 'win32' ? resolved.toLowerCase() : resolved;
        if (seen.has(key)) continue;
        seen.add(key);
        try {
            if (fs.existsSync(resolved) && fs.statSync(resolved).isFile()) result.push(resolved);
        } catch (_) { }
    }
    return result;
}

function wasmHostCandidates(roots = [], extensionDirectory = __dirname) {
    const candidates = [
        path.join(extensionDirectory, 'tools', 'absolute-wasm-host.js'),
        path.join(extensionDirectory, '..', 'tools', 'absolute-wasm-host.js')
    ];
    for (const root of roots) {
        candidates.push(path.join(root, 'tools', 'absolute-wasm-host.js'));
        candidates.push(path.join(root, 'Absolute', 'tools', 'absolute-wasm-host.js'));
    }
    return candidates;
}

function resolveWasmHost(roots = [], extensionDirectory = __dirname) {
    return uniqueExistingFiles(wasmHostCandidates(roots, extensionDirectory))[0];
}

function outputName(projectName, target) {
    return `${projectName}${isWasmTarget(target) ? '.wasm' : (process.platform === 'win32' ? '.exe' : '')}`;
}

module.exports = {
    normalizeTarget,
    isWasmTarget,
    projectTarget,
    projectRuntime,
    projectRunArgs,
    wasmHostCandidates,
    resolveWasmHost,
    outputName
};
