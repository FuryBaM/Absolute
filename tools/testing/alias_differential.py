#!/usr/bin/env python3
"""Build the suite twice at -O3, with and without type-based alias information.

Type-based alias information is a promise the backend makes to the optimizer:
that an access through one type cannot reach storage of another. The optimizer
believes it. A promise that is wrong is undefined behaviour rather than a slow
program -- a load hoisted out of a loop that a store inside the loop really did
change, a bounds check lifted that really could fail -- and nothing about the
wrong build looks wrong. It compiles, it runs, it prints a different answer.

Neither `-O0` nor a sanitizer can see this. At `-O0` the metadata is present
and unused, so the program is right; AddressSanitizer answers a different
question entirely, since a hoisted load reads memory it is perfectly entitled
to read. The only thing that shows a false tag is the difference between two
builds that must agree: the same program at the same optimization level, one
told what may alias and one told nothing, where "told nothing" is always safe.

So the oracle here is agreement, and neither side is a model of the semantics
that could be wrong on its own. A disagreement names a tag that lies.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path

SKIP_MARKERS = (
    "error", "errors", "invalid", "incomplete", "missing", "unterminated",
    "conflict", "legacy", "removed", "sanitizer", "plugin", "desktop",
)

SHARED_HOST = frozenset({
    "std-env", "std-fs", "std-http", "std-net", "std-net-udp", "std-process",
    "concurrency-stress", "wasm-fs", "wasm-full-runtime", "wasm-net",
    "wasm-wasi-services",
})


@dataclass
class Outcome:
    label: str
    exit_code: int
    output: str
    binary: Path | None = None


def run(command: list[str], timeout: int) -> subprocess.CompletedProcess:
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
    if source.stem in SHARED_HOST:
        return True
    return "concurrency" in name or "task-group" in name


def measure(compiler: Path, source: Path, work: Path, level: str,
            aliasing: bool, timeout: int) -> Outcome | None:
    """Build one way and run. None means it does not build, which is the
    test's own business and not a disagreement."""
    label = f"{level}{'' if aliasing else '+no-alias-info'}"
    binary = work / f"{source.stem}-{label.replace('+', '-')}"
    command = [str(compiler), str(source), f"-{level}", "--build-exe",
               "-o", str(binary)]
    if not aliasing:
        command.append("--no-type-alias-info")
    if run(command, timeout).returncode != 0:
        return None
    result = run([str(binary)], timeout)
    return Outcome(label, result.returncode, result.stdout + result.stderr,
                   binary)


def rerun(outcome: Outcome, timeout: int) -> Outcome | None:
    if outcome.binary is None:
        return None
    result = run([str(outcome.binary)], timeout)
    return Outcome(outcome.label, result.returncode,
                   result.stdout + result.stderr, outcome.binary)


def _diff_lines(left: str, right: str, limit: int = 6) -> list[str]:
    left_lines = left.splitlines()
    right_lines = right.splitlines()
    lines = []
    for index in range(max(len(left_lines), len(right_lines))):
        first = left_lines[index] if index < len(left_lines) else "<missing>"
        second = right_lines[index] if index < len(right_lines) else "<missing>"
        if first == second:
            continue
        lines.append(f"line {index + 1}: {first!r} vs {second!r}")
        if len(lines) >= limit:
            lines.append("...")
            break
    return lines


def compare_source(source: Path, compiler: Path, work: Path, levels: list[str],
                   timeout: int) -> tuple[int, int, int, list[str]]:
    lines: list[str] = []
    compared = 0
    unstable = 0
    disagreements = 0
    for level in levels:
        try:
            tagged = measure(compiler, source, work, level, True, timeout)
            untagged = measure(compiler, source, work, level, False, timeout)
        except subprocess.TimeoutExpired:
            lines.append(f"MISMATCH {source.name}: {level} timed out")
            disagreements += 1
            continue
        if not tagged or not untagged:
            continue

        try:
            repeat = rerun(tagged, timeout)
        except subprocess.TimeoutExpired:
            repeat = None
        if repeat and (repeat.output.rstrip() != tagged.output.rstrip() or
                       repeat.exit_code != tagged.exit_code):
            unstable += 1
            continue

        compared += 1
        if (tagged.exit_code == untagged.exit_code and
                tagged.output.rstrip() == untagged.output.rstrip()):
            continue
        disagreements += 1
        lines.append(
            f"MISMATCH {source.name}: {tagged.label} "
            f"exit={tagged.exit_code} vs {untagged.label} "
            f"exit={untagged.exit_code}")
        for line in _diff_lines(tagged.output, untagged.output):
            lines.append(f"    {line}")
    return compared, unstable, disagreements, lines


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--tests", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--levels", default="O2,O3",
                        help="comma separated; only optimized levels can show "
                             "this, because at -O0 the metadata is unused")
    parser.add_argument("--only", help="substring of the test name")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--jobs", type=int, default=0,
                        help="programs to compare at once; 0 uses the host cores")
    parser.add_argument("--configuration", default="")
    args = parser.parse_args()

    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    levels = [level.strip() for level in args.levels.split(",") if level.strip()]
    sources = runnable_tests(args.tests.resolve(), args.only)
    if not sources:
        print("alias-differential: no tests matched", file=sys.stderr)
        return 1

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
                            args.timeout)
                for source in parallel
            ]
            for future in as_completed(futures):
                accumulate(future.result())
    for source in exclusive:
        accumulate(compare_source(source, compiler, work, levels, args.timeout))

    print(f"alias-differential={'ok' if disagreements == 0 else 'failed'} "
          f"programs={compared} levels={len(levels)} jobs={jobs} "
          f"nondeterministic={unstable} disagreements={disagreements} "
          f"configuration={args.configuration}")
    return 0 if disagreements == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
