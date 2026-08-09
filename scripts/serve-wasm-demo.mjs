#!/usr/bin/env node
/**
 * Static file server with COOP/COEP so SharedArrayBuffer works in the browser.
 *
 *   node scripts/serve-wasm-demo.mjs [port] [root]
 *
 * Default root: examples/wasm
 * Also serves /tools/* from repo tools/ for worker module imports.
 */

import http from 'http';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(__dirname, '..');
// Port 0 = ephemeral (tests); default 5173 for interactive demos.
const portArg = process.argv[2];
const port = portArg === undefined || portArg === '' ? 5173 : Number(portArg);
const root = path.resolve(process.argv[3] || path.join(repoRoot, 'examples', 'wasm'));
const toolsRoot = path.join(repoRoot, 'tools');

const TYPES = {
    '.html': 'text/html; charset=utf-8',
    '.js': 'text/javascript; charset=utf-8',
    '.mjs': 'text/javascript; charset=utf-8',
    '.css': 'text/css; charset=utf-8',
    '.wasm': 'application/wasm',
    '.json': 'application/json',
    '.svg': 'image/svg+xml',
    '.png': 'image/png',
    '.jpg': 'image/jpeg',
};

function send(res, status, body, type = 'text/plain; charset=utf-8') {
    res.writeHead(status, {
        'Content-Type': type,
        'Content-Length': Buffer.byteLength(body),
        'Cross-Origin-Opener-Policy': 'same-origin',
        'Cross-Origin-Embedder-Policy': 'require-corp',
        'Cross-Origin-Resource-Policy': 'same-origin',
    });
    res.end(body);
}

function safeJoin(base, reqPath) {
    const decoded = decodeURIComponent(reqPath.split('?')[0]);
    const cleaned = path.normalize(decoded).replace(/^(\.\.[/\\])+/, '');
    const full = path.join(base, cleaned);
    if (!full.startsWith(base)) return null;
    return full;
}

const server = http.createServer((req, res) => {
    const urlPath = req.url || '/';
    let filePath = null;

    if (urlPath === '/tools' || urlPath.startsWith('/tools/')) {
        const rel = urlPath === '/tools' ? '' : urlPath.slice('/tools/'.length);
        filePath = safeJoin(toolsRoot, rel);
    } else {
        const rel = urlPath === '/' ? 'index.html' : urlPath;
        filePath = safeJoin(root, rel);
    }

    if (!filePath) {
        send(res, 403, 'forbidden');
        return;
    }

    fs.stat(filePath, (err, st) => {
        if (err || !st.isFile()) {
            send(res, 404, `not found: ${urlPath}`);
            return;
        }
        const ext = path.extname(filePath).toLowerCase();
        const type = TYPES[ext] || 'application/octet-stream';
        fs.readFile(filePath, (readErr, data) => {
            if (readErr) {
                send(res, 500, String(readErr));
                return;
            }
            res.writeHead(200, {
                'Content-Type': type,
                'Content-Length': data.length,
                'Cross-Origin-Opener-Policy': 'same-origin',
                'Cross-Origin-Embedder-Policy': 'require-corp',
                'Cross-Origin-Resource-Policy': 'same-origin',
            });
            res.end(data);
        });
    });
});

server.listen(port, '127.0.0.1', () => {
    const addr = server.address();
    const bound = typeof addr === 'object' && addr ? addr.port : port;
    // eslint-disable-next-line no-console
    console.log(`Absolute wasm demo (COOP/COEP) http://127.0.0.1:${bound}/`);
    // eslint-disable-next-line no-console
    console.log(`  root=${root}`);
    // eslint-disable-next-line no-console
    console.log(`  tools=${toolsRoot} via /tools/`);
    // eslint-disable-next-line no-console
    console.log(`  crossOriginIsolated should be true in DevTools`);
});
