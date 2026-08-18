#!/usr/bin/env python3
"""Run the test suite at every optimization level and compare the answers.

The generated corpora (optimization_differential.py, metamorphic_differential.py)
cover one program shape each, with parameters varied. The broadest corpus this
project has is `tests/` itself: strings, collections, ownership, exceptions,
JSON, the standard library. Nothing ran it at more than one optimization level,
and nothing compared it against another target, which is how a wasm shim that
parsed `2147483648` as -2147483648 while the native runtime answered 0 stayed
in the tree.

The oracle here is agreement, not a precomputed answer: -O0 and -O3 of the same
program must print the same thing and exit the same way. A program whose answer
depends on the optimization level has undefined behaviour in it -- that is how
the shift-by-width and the division-overflow defects showed themselves -- and
one that disagrees across targets has a runtime that does not match.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import os
from dataclasses import dataclass
from pathlib import Path

# Compiled but never run: these are refusals, entry points that need a plugin
# or a companion library, or programs that wait for input.
SKIP_MARKERS = (
    "error", "errors", "invalid", "incomplete", "missing", "unterminated",
    "conflict", "legacy", "removed", "sanitizer", "plugin", "desktop",
)

# Programs that answer to something outside themselves. They are compared
# across optimization levels like everything else -- the same binary, run the
# same way -- but not against WebAssembly, where the facility is absent by
# construction rather than by defect. `concurrency-stress` is the documented
# one: a wasm instance cannot wait on another worker, so a full channel drops
# messages (docs/wasm-target.md).
NEEDS_HOST_FACILITIES = frozenset({
    "std-env", "std-fs", "std-http", "std-net", "std-net-udp", "std-process",
    "concurrency-stress",
    # Registered for WebAssembly only -- their ctest entry builds for wasm32 and
    # runs the module under node -- and each asserts what the wasm host
    # provides: a virtual filesystem where a backslash is a separator (on Linux
    # it is an ordinary character in a name, documented in docs/wasm-target.md),
    # affinity that is unsupported there, the WASI services, the sandboxed
    # network. Natively they assert the opposite of what is true, by design.
    "wasm-fs", "wasm-full-runtime", "wasm-net", "wasm-wasi-services",
})


WASM_RUNNER = """'use strict';
const fs = require('fs');
const { instantiateAbsoluteWasm } = require(process.argv[2]);

async function main() {
    const bytes = fs.readFileSync(process.argv[3]);
    const session = await instantiateAbsoluteWasm(
        bytes, { captureLogs: true, allowNetwork: false });
    let code = 0;
    try {
        code = session.exports.main();
    } catch (error) {
        // A runtime check on wasm ends in a trap, because the shim's exit() is
        // __builtin_trap(): the module cannot hand an exit code back. The
        // program has already written its message to the log by then, so the
        // log is what gets compared, and the trap counts as the failing exit
        // the native build reports.
        code = 1;
    } finally {
        process.stdout.write(session.logs.join(''));
        try { session.host.shutdown(); } catch (_) {}
    }
    process.exitCode = code;
}

main().catch((error) => {
    console.error(error);
    process.exit(1);
});
"""


@dataclass
class Outcome:
    label: str
    exit_code: int
    output: str


def run(command: list[str], timeout: int) -> subprocess.CompletedProcess:
    return subprocess.run(
        command, capture_output=True, text=True, timeout=timeout, check=False)


def runnable_tests(directory: Path, only: str | None) -> list[Path]:
    sources = []
    for source in sorted(directory.glob("*.abs")):
        name = source.stem.lower()
        if any(marker in name for marker in SKIP_MARKERS):
            continue
        if only and only not in name:
            continue
        sources.append(source)
    return sources


def measure(compiler: Path, source: Path, work: Path, level: str,
            timeout: int) -> Outcome | None:
    """Build at one optimization level and run. None means it does not build,
    which is the test's own business and not a disagreement."""
    binary = work / f"{source.stem}-{level}"
    build = run([str(compiler), str(source), f"-{level}", "--build-exe",
                 "-o", str(binary)], timeout)
    if build.returncode != 0:
        return None
    result = run([str(binary)], timeout)
    return Outcome(level, result.returncode, result.stdout + result.stderr)


def measure_wasm(compiler: Path, source: Path, work: Path, node: Path,
                 wasm_host: Path, runner: Path, timeout: int) -> Outcome | None:
    module = work / f"{source.stem}.wasm"
    build = run([str(compiler), str(source), "--target",
                 "wasm32-unknown-unknown", "--build-exe", "-o", str(module)],
                timeout)
    if build.returncode != 0:
        return None
    result = run([str(node), str(runner), str(wasm_host), str(module)], timeout)
    # The JavaScript that starts the module prints its own stack trace when the
    # program exits through a runtime check. That is the harness talking, not
    # the program, and it has no counterpart on the native side.
    trace = [line for line in (result.stdout + result.stderr).splitlines()
             if not line.startswith("    at ") and not line.startswith("Error: ")]
    return Outcome("wasm", result.returncode, "\n".join(trace) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--tests", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--levels", default="O0,O2,O3",
                        help="comma separated, default O0,O2,O3")
    parser.add_argument("--only", default=None,
                        help="substring of the test name, for narrowing a hunt")
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--node", type=Path, default=None,
                        help="also compare the WebAssembly build, run with node")
    parser.add_argument("--wasm-host", type=Path, default=None,
                        help="tools/absolute-wasm-host.js")
    parser.add_argument("--configuration", default="")
    args = parser.parse_args()

    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    levels = [level.strip() for level in args.levels.split(",") if level.strip()]
    sources = runnable_tests(args.tests.resolve(), args.only)
    if not sources:
        print("suite-differential: no tests matched", file=sys.stderr)
        return 1

    runner = None
    if args.node and args.wasm_host:
        runner = work / "run-absolute-wasm.js"
        runner.write_text(WASM_RUNNER, encoding="utf-8")

    compared = 0
    unstable = 0
    disagreements = 0
    for source in sources:
        outcomes = []
        for level in levels:
            try:
                outcome = measure(args.compiler.resolve(), source, work, level,
                                  args.timeout)
            except subprocess.TimeoutExpired:
                print(f"MISMATCH {source.name}: {level} timed out")
                disagreements += 1
                outcome = None
            if outcome:
                outcomes.append(outcome)
        if runner and source.stem not in NEEDS_HOST_FACILITIES:
            try:
                wasm = measure_wasm(args.compiler.resolve(), source, work,
                                    args.node.resolve(), args.wasm_host.resolve(),
                                    runner, args.timeout)
            except subprocess.TimeoutExpired:
                wasm = None
            if wasm:
                outcomes.append(wasm)
        if len(outcomes) < 2:
            continue

        # A program that answers differently on two runs of the same build
        # answers for its own reasons -- a seed, a clock, a thread order -- and
        # cannot say anything about optimization levels. Left out rather than
        # reported, and counted so the number is visible.
        try:
            repeat = measure(args.compiler.resolve(), source, work, levels[0],
                             args.timeout)
        except subprocess.TimeoutExpired:
            repeat = None
        if repeat and (repeat.output.rstrip() != outcomes[0].output.rstrip() or
                       repeat.exit_code != outcomes[0].exit_code):
            unstable += 1
            continue
        compared += 1
        first = outcomes[0]
        for other in outcomes[1:]:
            # Compared without trailing blank lines: the two runners end their
            # output differently and that says nothing about the program.
            if (other.exit_code == first.exit_code and
                    other.output.rstrip() == first.output.rstrip()):
                continue
            disagreements += 1
            print(f"MISMATCH {source.name}: {first.label} exit={first.exit_code} "
                  f"vs {other.label} exit={other.exit_code}")
            for line in _diff_lines(first.output, other.output):
                print(f"    {line}")
            break

    print(f"suite-differential={'ok' if disagreements == 0 else 'failed'} "
          f"programs={compared} levels={len(levels)} "
          f"{'targets=native+wasm ' if runner else ''}"
          f"nondeterministic={unstable} "
          f"disagreements={disagreements} configuration={args.configuration}")
    return 0 if disagreements == 0 else 1


def _diff_lines(left: str, right: str, limit: int = 6) -> list[str]:
    left_lines = left.splitlines()
    right_lines = right.splitlines()
    shown = []
    for index in range(max(len(left_lines), len(right_lines))):
        first = left_lines[index] if index < len(left_lines) else "<nothing>"
        second = right_lines[index] if index < len(right_lines) else "<nothing>"
        if first != second:
            shown.append(f"line {index + 1}: {first!r} vs {second!r}")
        if len(shown) >= limit:
            break
    return shown


if __name__ == "__main__":
    sys.exit(main())
