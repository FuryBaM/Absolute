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
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
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
    binary: Path | None = None


def run(command: list[str], timeout: int) -> subprocess.CompletedProcess:
    # Closed standard input, not the runner's own. A program in the suite that
    # reads until end of file -- and one does -- otherwise blocks on whatever
    # the caller's stdin happened to be, so the same corpus finished in seconds
    # from one shell and sat there until the timeout from another. The answer a
    # test gives must not depend on who started it.
    return subprocess.run(
        command, capture_output=True, text=True, timeout=timeout, check=False,
        stdin=subprocess.DEVNULL)


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


def uses_shared_host(source: Path) -> bool:
    name = source.stem.lower()
    if source.stem in NEEDS_HOST_FACILITIES:
        return True
    return "concurrency" in name or "task-group" in name


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
    return Outcome(level, result.returncode, result.stdout + result.stderr,
                   binary)


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


def rerun(outcome: Outcome, timeout: int) -> Outcome | None:
    if outcome.binary is None:
        return None
    result = run([str(outcome.binary)], timeout)
    return Outcome(outcome.label, result.returncode,
                   result.stdout + result.stderr, outcome.binary)


def compare_source(source: Path, compiler: Path, work: Path, levels: list[str],
                   timeout: int, runner: Path | None, node: Path | None,
                   wasm_host: Path | None) -> tuple[int, int, int, list[str]]:
    lines: list[str] = []
    outcomes: list[Outcome] = []
    for level in levels:
        try:
            outcome = measure(compiler, source, work, level, timeout)
        except subprocess.TimeoutExpired:
            lines.append(f"MISMATCH {source.name}: {level} timed out")
            return 0, 0, 1, lines
        if outcome:
            outcomes.append(outcome)
    if runner and node and wasm_host and source.stem not in NEEDS_HOST_FACILITIES:
        try:
            wasm = measure_wasm(compiler, source, work, node, wasm_host,
                                runner, timeout)
        except subprocess.TimeoutExpired:
            wasm = None
        if wasm:
            outcomes.append(wasm)
    if len(outcomes) < 2:
        return 0, 0, 0, lines

    # Two runs of the same build, not two rebuilds: a seed, a clock, or a
    # thread order is not a disagreement between optimization levels.
    try:
        repeat = rerun(outcomes[0], timeout)
    except subprocess.TimeoutExpired:
        repeat = None
    if repeat and (repeat.output.rstrip() != outcomes[0].output.rstrip() or
                   repeat.exit_code != outcomes[0].exit_code):
        return 0, 1, 0, lines

    first = outcomes[0]
    for other in outcomes[1:]:
        if (other.exit_code == first.exit_code and
                other.output.rstrip() == first.output.rstrip()):
            continue
        lines.append(
            f"MISMATCH {source.name}: {first.label} exit={first.exit_code} "
            f"vs {other.label} exit={other.exit_code}")
        for line in _diff_lines(first.output, other.output):
            lines.append(f"    {line}")
        return 1, 0, 1, lines
    return 1, 0, 0, lines


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
    parser.add_argument("--jobs", type=int, default=0,
                        help="programs to compare at once; 0 uses the host cores")
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

    # Asking for the WebAssembly axis takes both a node to run it and the host
    # that loads the module. Giving one without the other used to run the
    # native comparison alone and report success, so a run that was meant to
    # compare two targets compared one and said `ok` -- the worst answer a
    # harness can give, because it is indistinguishable from having done the
    # work. Either both are present or the run refuses.
    if bool(args.node) != bool(args.wasm_host):
        print("suite-differential: --node and --wasm-host go together; "
              "the WebAssembly axis needs a runtime and the host that loads "
              "the module", file=sys.stderr)
        return 2
    runner = None
    node = None
    wasm_host = None
    if args.node and args.wasm_host:
        runner = work / "run-absolute-wasm.js"
        runner.write_text(WASM_RUNNER, encoding="utf-8")
        node = args.node.resolve()
        wasm_host = args.wasm_host.resolve()

    compiler = args.compiler.resolve()
    jobs = args.jobs if args.jobs > 0 else (os.cpu_count() or 2)
    compared = 0
    unstable = 0
    disagreements = 0

    def accumulate(report: tuple[int, int, int, list[str]]) -> None:
        nonlocal compared, unstable, disagreements
        extra_compared, extra_unstable, extra_disagreements, lines = report
        compared += extra_compared
        unstable += extra_unstable
        disagreements += extra_disagreements
        for line in lines:
            print(line, flush=True)

    exclusive = [source for source in sources if uses_shared_host(source)]
    parallel = [source for source in sources if not uses_shared_host(source)]
    if jobs <= 1:
        parallel, exclusive = sources, []

    if parallel:
        workers = 1 if jobs <= 1 else min(jobs, len(parallel))
        with ThreadPoolExecutor(max_workers=workers) as pool:
            futures = [
                pool.submit(compare_source, source, compiler, work, levels,
                            args.timeout, runner, node, wasm_host)
                for source in parallel
            ]
            for future in as_completed(futures):
                accumulate(future.result())
    for source in exclusive:
        accumulate(compare_source(source, compiler, work, levels, args.timeout,
                                  runner, node, wasm_host))

    print(f"suite-differential={'ok' if disagreements == 0 else 'failed'} "
          f"programs={compared} levels={len(levels)} jobs={jobs} "
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
