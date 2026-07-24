#!/usr/bin/env node
'use strict';

/**
 * Absolute developer CLI:
 *   node tools/absolute-dev.js fmt [files...]
 *   node tools/absolute-dev.js test [ctest-args...]
 *   node tools/absolute-dev.js doc [roots...] [-o out.md]
 *   node tools/absolute-dev.js package list|resolve [project.absproj]
 */

const fs = require('fs');
const path = require('path');
const childProcess = require('child_process');

const repoRoot = path.resolve(__dirname, '..');
const language = require(path.join(repoRoot, 'absolute-extension', 'server', 'language.js'));

function usage() {
    console.log(`Usage:
  absolute-dev fmt [file.abs ...]
  absolute-dev test [-- [ctest args...]]
  absolute-dev doc [root ...] [-o out.md]
  absolute-dev package list <project.absproj>
  absolute-dev package resolve <project.absproj>
`);
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
    default:
        console.error(`unknown command: ${command}`);
        usage();
        return 1;
    }
}

process.exit(main(process.argv.slice(2)));
