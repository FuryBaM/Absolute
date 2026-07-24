'use strict';

/**
 * Repo-root CLI entry for Absolute REPL.
 * Implementation lives in absolute-extension/tools so the VS Code VSIX is self-contained.
 */
module.exports = require('../absolute-extension/tools/absolute-repl.js');

if (require.main === module) {
    // Delegate to the packaged implementation's CLI (re-require as main is awkward;
    // re-run the same argv handling here).
    const repl = module.exports;
    const args = process.argv.slice(2);
    if (args[0] === '-e' || args[0] === '--eval') {
        const result = repl.evaluate(args.slice(1).join(' '));
        if (result.stdout) process.stdout.write(result.stdout);
        if (result.stderr) process.stderr.write(result.stderr);
        process.exit(result.ok ? 0 : 1);
    } else {
        repl.startRepl();
    }
}
