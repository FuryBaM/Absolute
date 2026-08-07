#!/usr/bin/env python3
"""Report which parts of the front end a fuzzing corpus actually reaches.

libFuzzer prints an edge count, which says how much of the instrumented code
was hit but not where the gaps are. A corpus can sit at a high number while
never entering, say, interface declarations, because most of its inputs are
variations of the same closing bracket. This maps the corpus back onto source
files and reports the components separately, so a target that has stopped
making progress is visible.

Build the targets with -DABSOLUTE_FUZZ_COVERAGE=ON first; the flags it adds
are what produce the profile.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

# Reported separately because they fail independently: a corpus can saturate
# the lexer and never reach semantic analysis at all.
COMPONENTS = {
    "lexer": ("Absolute-Parser/src/lexer",),
    "parser": ("Absolute-Parser/src/parser",),
    "analyzer": ("Absolute-Analyzer/src",),
    "codegen": ("Absolute-CodeGen/src",),
}


def tool(name: str) -> str:
    for candidate in (f"{name}-18", name):
        found = shutil.which(candidate)
        if found:
            return found
    raise SystemExit(f"{name} was not found; install the LLVM tools")


def run(command: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command, capture_output=True, text=True, encoding="utf-8",
        errors="replace", **kwargs)


def collect(fuzzer: Path, corpus: Path, work: Path) -> Path:
    """Run the target over every corpus input and merge the raw profiles."""
    raw = work / "raw"
    if raw.exists():
        shutil.rmtree(raw)
    raw.mkdir(parents=True)

    environment = os.environ.copy()
    # %m keeps one profile per process image instead of overwriting.
    environment["LLVM_PROFILE_FILE"] = str(raw / "profile-%p-%m.profraw")
    inputs = [str(item) for item in sorted(corpus.iterdir()) if item.is_file()]
    if not inputs:
        raise SystemExit(f"corpus is empty: {corpus}")
    # -runs=0 replays the corpus without generating new input.
    subprocess.run(
        [str(fuzzer), *inputs, "-runs=0"],
        capture_output=True, text=True, env=environment, timeout=1800)

    profiles = sorted(raw.glob("*.profraw"))
    if not profiles:
        raise SystemExit(
            "no profile was written; configure with -DABSOLUTE_FUZZ_COVERAGE=ON")
    merged = work / "merged.profdata"
    result = run([tool("llvm-profdata"), "merge", "-sparse",
                  *[str(item) for item in profiles], "-o", str(merged)])
    if result.returncode != 0:
        raise SystemExit(f"llvm-profdata failed:\n{result.stderr}")
    return merged


def report(fuzzer: Path, merged: Path, repository: Path) -> dict[str, tuple[int, int]]:
    result = run([
        tool("llvm-cov"), "report", str(fuzzer),
        f"-instr-profile={merged}",
    ], cwd=repository)
    if result.returncode != 0:
        raise SystemExit(f"llvm-cov failed:\n{result.stderr}")

    totals: dict[str, list[int]] = {name: [0, 0] for name in COMPONENTS}
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) < 4 or not fields[1].isdigit():
            continue
        path, regions, missed = fields[0], int(fields[1]), int(fields[2])
        for name, prefixes in COMPONENTS.items():
            if any(prefix in path for prefix in prefixes):
                totals[name][0] += regions
                totals[name][1] += regions - missed
                break
    return {name: (values[0], values[1]) for name, values in totals.items()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fuzzer", required=True, type=Path)
    parser.add_argument("--corpus", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument("--name", default="")
    args = parser.parse_args()

    fuzzer = args.fuzzer.resolve()
    if not fuzzer.exists():
        parser.error(f"fuzzer does not exist: {fuzzer}")
    corpus = args.corpus.resolve()
    if not corpus.is_dir():
        parser.error(f"corpus is not a directory: {corpus}")

    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    merged = collect(fuzzer, corpus, work)
    totals = report(fuzzer, merged, args.repository.resolve())

    name = args.name or fuzzer.name
    print(f"coverage for {name} over {corpus}")
    print(f"{'component':12}{'regions':>10}{'covered':>10}{'percent':>10}")
    for component, (regions, covered) in totals.items():
        if regions == 0:
            print(f"{component:12}{'-':>10}{'-':>10}{'not linked':>10}")
            continue
        share = 100.0 * covered / regions
        print(f"{component:12}{regions:>10}{covered:>10}{share:>9.1f}%")
    print(f"profile: {merged}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
