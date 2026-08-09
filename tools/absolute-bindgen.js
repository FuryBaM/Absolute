#!/usr/bin/env node
'use strict';

/**
 * Absolute C header bindgen (v1).
 *
 * Prefers clang AST JSON dump from the portable LLVM SDK or PATH.
 * Emits Absolute `extern "C"` declarations plus simple `using` aliases for
 * pointer/scalar typedefs. Unsupported constructs are emitted as comments.
 *
 * Usage:
 *   node tools/absolute-bindgen.js <header.h> [-o out.abs] [-I dir ...]
 *       [--clang path] [--module Name] [--keep-const-char-as-raw]
 */

const fs = require('fs');
const path = require('path');
const os = require('os');
const childProcess = require('child_process');

const repoRoot = path.resolve(__dirname, '..');

function usage() {
    console.error(`Usage:
  absolute-bindgen <header.h> [-o out.abs] [-I includeDir ...]
      [--clang path/to/clang] [--module Name] [--keep-const-char-as-raw]
      [--allow-system]

Maps C declarations to Absolute extern "C" / using aliases (C ABI only).
Requires clang (PATH or .absolute/toolchains/llvm-*/bin/clang).`);
}

function findClang(explicit) {
    if (explicit && fs.existsSync(explicit)) return explicit;
    if (process.env.ABSOLUTE_CLANG && fs.existsSync(process.env.ABSOLUTE_CLANG))
        return process.env.ABSOLUTE_CLANG;
    const toolchains = path.join(repoRoot, '.absolute', 'toolchains');
    if (fs.existsSync(toolchains)) {
        const entries = fs.readdirSync(toolchains)
            .filter(name => name.startsWith('llvm-'))
            .sort()
            .reverse();
        for (const name of entries) {
            const candidate = path.join(toolchains, name, 'bin',
                process.platform === 'win32' ? 'clang.exe' : 'clang');
            if (fs.existsSync(candidate)) return candidate;
        }
    }
    const which = childProcess.spawnSync(
        process.platform === 'win32' ? 'where' : 'which',
        ['clang'],
        { encoding: 'utf8', windowsHide: true });
    const first = (which.stdout || '').split(/\r?\n/).map(s => s.trim()).find(Boolean);
    if (first && fs.existsSync(first)) return first;
    return null;
}

function stripBom(text) {
    return text.charCodeAt(0) === 0xfeff ? text.slice(1) : text;
}

function normalizePath(p) {
    return path.resolve(p).replace(/\\/g, '/').toLowerCase();
}

function isSystemPath(filePath) {
    if (!filePath) return true;
    const n = filePath.replace(/\\/g, '/').toLowerCase();
    return n.includes('/usr/include') ||
        n.includes('/lib/clang/') ||
        n.includes('/lib64/clang/') ||
        n.includes('/windows kits/') ||
        n.includes('/microsoft visual studio/') ||
        n.includes('/ucrt/') ||
        n.includes('/include/10.0') ||
        n.includes('\\windows kits\\') ||
        n.includes('\\microsoft visual studio\\');
}

function walkAst(node, visit) {
    if (!node || typeof node !== 'object') return;
    if (Array.isArray(node)) {
        for (const child of node) walkAst(child, visit);
        return;
    }
    visit(node);
    if (node.inner) walkAst(node.inner, visit);
}

function parseQualType(qualType) {
    return String(qualType || '').trim();
}

function mapCType(qualType, options) {
    let t = parseQualType(qualType);
    if (!t) return { ok: false, reason: 'empty type' };

    // Drop calling-convention / attributes noise.
    t = t.replace(/\b(__attribute__|__declspec)\s*\([^)]*\)/g, '').trim();
    t = t.replace(/\b(_Nonnull|_Nullable|_Null_unspecified)\b/g, '').trim();
    t = t.replace(/\s+/g, ' ');

    // Named typedef aliases emitted as Absolute `using`.
    if (options.pointerAliases && options.pointerAliases.has(t))
        return { ok: true, abs: t };
    if (options.typedefs && options.typedefs.has(t))
        return { ok: true, abs: options.typedefs.get(t).abs, note: options.typedefs.get(t).note };

    // Function pointer: Ret (*)(Args) or Ret (Name*)(Args)
    const fnPtr = t.match(/^(.+?)\s*\(\s*\*?\s*[^)]*\)\s*\((.*)\)$/);
    if (fnPtr) {
        const ret = mapCType(fnPtr[1].trim(), options);
        if (!ret.ok) return ret;
        const argsRaw = fnPtr[2].trim();
        if (!argsRaw || argsRaw === 'void') {
            return { ok: true, abs: `cfunc<${ret.abs}>` };
        }
        const args = splitTopLevel(argsRaw, ',').map(a => mapCType(a.trim(), options));
        for (const a of args) if (!a.ok) return a;
        return { ok: true, abs: `cfunc<${ret.abs}, ${args.map(a => a.abs).join(', ')}>` };
    }

    // Pointers (peel const/volatile from either side).
    let isConst = false;
    const pointerMatch = t.match(/^(.*)\*\s*(const|volatile)?\s*$/);
    if (pointerMatch || t.endsWith('*')) {
        let base = t;
        while (base.endsWith('*') || /\*\s*(const|volatile)?\s*$/.test(base)) {
            base = base.replace(/\s*\*\s*(const|volatile)?\s*$/, '').trim();
        }
        const quals = t.match(/\bconst\b/g) || [];
        isConst = quals.length > 0;
        base = base.replace(/\b(const|volatile|restrict)\b/g, '').replace(/\s+/g, ' ').trim();

        if (base === 'void') return { ok: true, abs: 'raw void*' };
        if (base === 'char' || base === 'signed char') {
            if (isConst && !options.keepConstCharAsRaw)
                return { ok: true, abs: 'string', note: 'const char* -> string (caller-owned UTF-8)' };
            return { ok: true, abs: 'raw int8*' };
        }
        if (base === 'unsigned char' || base === 'uint8_t')
            return { ok: true, abs: 'raw uint8*' };
        if (base === 'int8_t' || base === 'int16_t' || base === 'int32_t' || base === 'int64_t' ||
            base === 'uint16_t' || base === 'uint32_t' || base === 'uint64_t' ||
            base === 'int' || base === 'short' || base === 'long' || base === 'float' ||
            base === 'double' || base === '_Bool' || base === 'bool') {
            const mapped = mapScalar(base);
            if (!mapped.ok) return mapped;
            return { ok: true, abs: `raw ${mapped.abs}*` };
        }
        // Opaque / typedef pointer.
        if (/^[A-Za-z_][A-Za-z0-9_]*$/.test(base)) {
            if (options.pointerAliases && options.pointerAliases.has(base))
                return { ok: true, abs: base };
            if (options.typedefs && options.typedefs.has(base))
                return { ok: true, abs: `raw ${options.typedefs.get(base).abs}*` };
            return { ok: true, abs: 'raw void*', note: `opaque pointer to ${base}` };
        }
        return { ok: false, reason: `unsupported pointer type '${t}'` };
    }

    t = t.replace(/\b(const|volatile|restrict)\b/g, '').replace(/\s+/g, ' ').trim();
    return mapScalar(t);
}

function mapScalar(t) {
    const table = {
        void: 'void',
        _Bool: 'bool',
        bool: 'bool',
        char: 'char',
        'signed char': 'int8',
        'unsigned char': 'uint8',
        short: 'int16',
        'short int': 'int16',
        'signed short': 'int16',
        'signed short int': 'int16',
        'unsigned short': 'uint16',
        'unsigned short int': 'uint16',
        int: 'int32',
        signed: 'int32',
        'signed int': 'int32',
        unsigned: 'uint32',
        'unsigned int': 'uint32',
        // long is platform-dependent; Absolute prefers fixed widths.
        long: 'int64',
        'long int': 'int64',
        'signed long': 'int64',
        'signed long int': 'int64',
        'unsigned long': 'uint64',
        'unsigned long int': 'uint64',
        'long long': 'int64',
        'long long int': 'int64',
        'signed long long': 'int64',
        'unsigned long long': 'uint64',
        'unsigned long long int': 'uint64',
        float: 'float',
        double: 'double',
        int8_t: 'int8',
        uint8_t: 'uint8',
        int16_t: 'int16',
        uint16_t: 'uint16',
        int32_t: 'int32',
        uint32_t: 'uint32',
        int64_t: 'int64',
        uint64_t: 'uint64',
        size_t: 'uint64',
        ptrdiff_t: 'int64',
        intptr_t: 'int64',
        uintptr_t: 'uint64',
    };
    if (table[t]) {
        const note = (t === 'long' || t === 'unsigned long' || t === 'size_t')
            ? `${t} mapped to fixed Absolute width; verify platform ABI`
            : undefined;
        return { ok: true, abs: table[t], note };
    }
    if (/^[A-Za-z_][A-Za-z0-9_]*$/.test(t))
        return { ok: false, reason: `unsupported or incomplete type '${t}' (use pointer form)` };
    return { ok: false, reason: `unsupported type '${t}'` };
}

function splitTopLevel(text, sep) {
    const parts = [];
    let depth = 0;
    let start = 0;
    for (let i = 0; i < text.length; ++i) {
        const ch = text[i];
        if (ch === '(' || ch === '[' || ch === '<') depth += 1;
        else if (ch === ')' || ch === ']' || ch === '>') depth = Math.max(0, depth - 1);
        else if (ch === sep && depth === 0) {
            parts.push(text.slice(start, i));
            start = i + 1;
        }
    }
    parts.push(text.slice(start));
    return parts;
}

function sanitizeIdent(name, fallback) {
    if (name && /^[A-Za-z_][A-Za-z0-9_]*$/.test(name)) return name;
    return fallback;
}

function dumpAstJson(clang, headerPath, includeDirs) {
    const args = [
        '-Xclang', '-ast-dump=json',
        '-fsyntax-only',
        '-x', 'c',
        '-std=c11',
        '-Wno-everything',
    ];
    for (const dir of includeDirs) {
        args.push('-I', dir);
    }
    // Make bool available in freestanding headers.
    args.push('-include', 'stdbool.h');
    args.push(headerPath);

    const result = childProcess.spawnSync(clang, args, {
        encoding: 'utf8',
        maxBuffer: 64 * 1024 * 1024,
        windowsHide: true,
    });
    if (result.error) throw result.error;
    // clang may write diagnostics to stderr; JSON is on stdout.
    const stdout = stripBom(result.stdout || '');
    if (!stdout.trim().startsWith('{')) {
        const err = (result.stderr || '').trim() || 'clang produced no AST JSON';
        throw new Error(`clang AST dump failed (exit ${result.status}): ${err}`);
    }
    return JSON.parse(stdout);
}

function collectFromAst(ast, headerPath, options) {
    const headerNorm = normalizePath(headerPath);
    const headerBase = path.basename(headerPath).toLowerCase();
    const headerText = fs.readFileSync(headerPath, 'utf8');
    const typedefs = new Map(); // name -> { abs, note? }
    const pointerAliases = new Set();
    const functions = [];
    const skipped = [];
    let lastFile = '';

    walkAst(ast, (node) => {
        if (node.loc && node.loc.file) lastFile = node.loc.file;
        const file = (node.loc && node.loc.file) || lastFile || '';
        const fileNorm = file ? normalizePath(file) : '';
        const inMainHeader = fileNorm === headerNorm ||
            path.basename(file).toLowerCase() === headerBase ||
            (!file && headerText.includes(node.name || '\0'));

        if (node.kind === 'TypedefDecl' && node.name && inMainHeader && !options.allowSystem) {
            if (isSystemPath(file) && fileNorm !== headerNorm) return;
            const qual = node.type && (node.type.qualType || node.type.desugaredQualType);
            const mapped = mapCType(qual, { ...options, typedefs, pointerAliases });
            if (mapped.ok) {
                typedefs.set(node.name, mapped);
                if (mapped.abs === 'raw void*' || mapped.abs.startsWith('raw '))
                    pointerAliases.add(node.name);
            } else {
                skipped.push({ kind: 'typedef', name: node.name, reason: mapped.reason });
            }
            return;
        }

        if (node.kind !== 'FunctionDecl' || !node.name || node.isImplicit) return;
        if (node.inline) return;
        const hasBody = Array.isArray(node.inner) &&
            node.inner.some(child => child.kind === 'CompoundStmt');
        if (hasBody) return;

        // Prefer declarations that appear in the source header text.
        const mentioned = new RegExp(`\\b${node.name}\\s*\\(`).test(headerText);
        if (!mentioned) return;
        if (file && isSystemPath(file) && fileNorm !== headerNorm) return;

        const params = [];
        let variadic = !!node.variadic;
        for (const child of node.inner || []) {
            if (child.kind !== 'ParmVarDecl') continue;
            const qual = child.type && (child.type.qualType || child.type.desugaredQualType);
            const mapped = mapCType(qual, { ...options, typedefs, pointerAliases });
            const pname = sanitizeIdent(child.name, `arg${params.length}`);
            if (!mapped.ok) {
                skipped.push({
                    kind: 'function',
                    name: node.name,
                    reason: `parameter '${pname}': ${mapped.reason}`,
                });
                return;
            }
            params.push({ name: pname, type: mapped.abs, note: mapped.note });
        }

        const typeQual = node.type && node.type.qualType;
        // Return type is before '(' in "Ret (Args)"
        let returnQual = 'void';
        if (typeQual) {
            const idx = typeQual.indexOf('(');
            returnQual = idx === -1 ? typeQual : typeQual.slice(0, idx).trim();
        }
        const ret = mapCType(returnQual, { ...options, typedefs, pointerAliases });
        if (!ret.ok) {
            skipped.push({ kind: 'function', name: node.name, reason: `return: ${ret.reason}` });
            return;
        }
        if (variadic) {
            skipped.push({ kind: 'function', name: node.name, reason: 'variadic functions are not supported' });
            return;
        }

        functions.push({
            name: node.name,
            returnType: ret.abs,
            returnNote: ret.note,
            parameters: params,
        });
    });

    // Deduplicate by name (prefer first).
    const seen = new Set();
    const uniqueFunctions = [];
    for (const fn of functions) {
        if (seen.has(fn.name)) continue;
        seen.add(fn.name);
        uniqueFunctions.push(fn);
    }

    return { typedefs, pointerAliases, functions: uniqueFunctions, skipped };
}

function renderAbsolute(headerPath, collected, options) {
    const lines = [];
    const rel = path.relative(process.cwd(), headerPath).replace(/\\/g, '/') || headerPath;
    lines.push('// Generated by absolute-bindgen. Do not edit by hand.');
    lines.push(`// Source: ${rel}`);
    lines.push('// C ABI only - see docs/native-c-abi.md');
    lines.push('');

    if (options.module) {
        lines.push(`namespace ${options.module} {`);
        lines.push('');
    }

    if (collected.typedefs.size) {
        lines.push('// Type aliases (pointer/scalar typedefs)');
        for (const [name, mapped] of collected.typedefs) {
            if (mapped.note) lines.push(`// note: ${mapped.note}`);
            // Only emit pointer/scalar aliases that Absolute can express.
            if (mapped.abs === 'raw void*' || mapped.abs.startsWith('raw ') ||
                ['int8', 'uint8', 'int16', 'uint16', 'int32', 'uint32', 'int64', 'uint64',
                    'float', 'double', 'bool', 'char', 'string'].includes(mapped.abs)) {
                lines.push(`using ${name} = ${mapped.abs};`);
            } else {
                lines.push(`// skipped typedef ${name} = ${mapped.abs}`);
            }
        }
        lines.push('');
    }

    if (collected.functions.length) {
        lines.push('// Functions');
        for (const fn of collected.functions) {
            if (fn.returnNote) lines.push(`// return note: ${fn.returnNote}`);
            for (const p of fn.parameters) {
                if (p.note) lines.push(`// ${p.name}: ${p.note}`);
            }
            const params = fn.parameters.map(p => `${p.type} ${p.name}`).join(', ');
            lines.push(`extern "C" ${fn.returnType} ${fn.name}(${params});`);
        }
        lines.push('');
    } else {
        lines.push('// No bindable function declarations were found.');
        lines.push('');
    }

    if (collected.skipped.length) {
        lines.push('// Skipped declarations:');
        for (const item of collected.skipped) {
            lines.push(`// - ${item.kind} ${item.name}: ${item.reason}`);
        }
        lines.push('');
    }

    if (options.module) {
        lines.push(`} // namespace ${options.module}`);
        lines.push('');
    }

    return lines.join('\n');
}

function parseArgs(argv) {
    const options = {
        header: null,
        output: null,
        clang: null,
        includes: [],
        module: null,
        keepConstCharAsRaw: false,
        allowSystem: false,
    };
    const args = argv.slice();
    while (args.length) {
        const arg = args.shift();
        if (arg === '-h' || arg === '--help') {
            usage();
            process.exit(0);
        }
        if (arg === '-o' || arg === '--output') {
            options.output = args.shift();
            continue;
        }
        if (arg === '-I' || arg === '--include') {
            options.includes.push(args.shift());
            continue;
        }
        if (arg === '--clang') {
            options.clang = args.shift();
            continue;
        }
        if (arg === '--module') {
            options.module = args.shift();
            continue;
        }
        if (arg === '--keep-const-char-as-raw') {
            options.keepConstCharAsRaw = true;
            continue;
        }
        if (arg === '--allow-system') {
            options.allowSystem = true;
            continue;
        }
        if (arg.startsWith('-')) {
            throw new Error(`Unknown option: ${arg}`);
        }
        if (options.header) throw new Error('Only one header file is supported');
        options.header = arg;
    }
    if (!options.header) {
        usage();
        process.exit(2);
    }
    return options;
}

function main(argv) {
    const options = parseArgs(argv);
    const headerPath = path.resolve(options.header);
    if (!fs.existsSync(headerPath)) {
        console.error(`Header not found: ${headerPath}`);
        return 1;
    }

    const clang = findClang(options.clang);
    if (!clang) {
        console.error('clang was not found. Install LLVM or bootstrap Absolute Windows toolchains.');
        console.error('Looked in PATH and .absolute/toolchains/llvm-*/bin/clang');
        return 1;
    }

    const includes = options.includes.slice();
    includes.push(path.dirname(headerPath));

    let ast;
    try {
        ast = dumpAstJson(clang, headerPath, includes);
    } catch (error) {
        console.error(String(error.message || error));
        return 1;
    }

    const mapOptions = {
        keepConstCharAsRaw: options.keepConstCharAsRaw,
        allowSystem: options.allowSystem,
    };
    const collected = collectFromAst(ast, headerPath, mapOptions);
    // Re-map typedef-aware pointer aliases into a second pass for function params
    // by rebuilding with discovered pointerAliases.
    mapOptions.typedefs = collected.typedefs;
    mapOptions.pointerAliases = collected.pointerAliases;
    const refined = collectFromAst(ast, headerPath, mapOptions);

    const text = renderAbsolute(headerPath, refined, {
        module: options.module,
    });

    if (options.output) {
        const outPath = path.resolve(options.output);
        fs.mkdirSync(path.dirname(outPath), { recursive: true });
        fs.writeFileSync(outPath, text, 'utf8');
        console.error(`wrote ${outPath} (${refined.functions.length} function(s), ${refined.typedefs.size} typedef(s))`);
    } else {
        process.stdout.write(text);
    }
    return 0;
}

if (require.main === module) {
    try {
        process.exit(main(process.argv.slice(2)));
    } catch (error) {
        console.error(error.stack || error);
        process.exit(1);
    }
}

module.exports = {
    findClang,
    mapCType,
    collectFromAst,
    renderAbsolute,
    dumpAstJson,
    main,
};
