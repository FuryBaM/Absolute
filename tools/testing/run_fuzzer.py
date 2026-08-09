#!/usr/bin/env python3
"""Drive a libFuzzer target: replay regressions, fuzz a budget, minimize.

The budget is deliberately short by default so the fuzzers can run on every
pull request. `ABSOLUTE_FUZZ_SECONDS` widens it for nightly and weekly runs
without changing the test definition.

Two corpora are kept in the repository and serve different purposes. The seed
corpus gives the fuzzer valid programs to mutate, so it starts from real
language shapes instead of rediscovering that a file begins with a keyword.
The regression corpus holds inputs that once crashed; every one of them is
replayed first, so a fixed crash cannot come back unnoticed.

A crash is minimized before it is reported, and the exact command to reproduce
it is printed rather than left for the reader to reconstruct.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

DEFAULT_BUDGET_SECONDS = 30


def run(command: list[str], timeout: float | None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
        env=os.environ.copy(),
    )


def reproduce_command(fuzzer: Path, case: Path) -> str:
    return f"{fuzzer} {case}"


def replay_regressions(fuzzer: Path, corpus: Path, name: str) -> int:
    """Every previously crashing input must still be handled."""
    if not corpus.is_dir():
        return 0
    cases = sorted(
        item for item in corpus.iterdir()
        if item.is_file() and item.suffix != ".md"
    )
    for case in cases:
        result = run([str(fuzzer), str(case)], timeout=120)
        if result.returncode != 0:
            print(
                f"fuzz-{name}=failed regression {case.name} still crashes\n"
                f"reproduce: {reproduce_command(fuzzer, case)}\n"
                f"{result.stderr[-4000:]}",
                file=sys.stderr,
            )
            return 1
    print(f"fuzz-{name}: replayed {len(cases)} regression inputs")
    return 0


def minimize(fuzzer: Path, crash: Path, work: Path, name: str) -> Path:
    """Shrink a crashing input; return the smallest one that still crashes."""
    minimized = work / f"minimized-{crash.name}"
    shutil.copyfile(crash, minimized)
    result = run(
        [
            str(fuzzer),
            "-minimize_crash=1",
            f"-runs=20000",
            f"-exact_artifact_path={minimized}",
            str(crash),
        ],
        timeout=300,
    )
    # libFuzzer writes the reduced input to exact_artifact_path; if it could
    # not improve on the original the copy above is still the reproducer.
    del result
    return minimized


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fuzzer", required=True, type=Path)
    parser.add_argument("--name", required=True)
    parser.add_argument("--seed-corpus", type=Path)
    parser.add_argument("--regression-corpus", type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--budget-seconds", default="")
    parser.add_argument("--max-input", type=int, default=4096)
    args = parser.parse_args()

    fuzzer = args.fuzzer.resolve()
    if not fuzzer.exists():
        parser.error(f"fuzzer does not exist: {fuzzer}")

    budget = DEFAULT_BUDGET_SECONDS
    requested = (args.budget_seconds or "").strip()
    if requested:
        try:
            budget = max(1, int(requested))
        except ValueError:
            parser.error(f"invalid --budget-seconds: {requested}")

    work = args.work_dir.resolve()
    if work.exists():
        shutil.rmtree(work)
    corpus = work / "corpus"
    corpus.mkdir(parents=True, exist_ok=True)

    if args.regression_corpus is not None:
        status = replay_regressions(fuzzer, args.regression_corpus, args.name)
        if status != 0:
            return status

    # Work on a copy: the fuzzer adds anything interesting it finds, and the
    # checked-in seed corpus should not change as a side effect of a test run.
    seeds = 0
    if args.seed_corpus is not None and args.seed_corpus.is_dir():
        for item in sorted(args.seed_corpus.iterdir()):
            if item.is_file():
                shutil.copyfile(item, corpus / item.name)
                seeds += 1

    artifacts = work / "artifacts"
    artifacts.mkdir(parents=True, exist_ok=True)
    command = [
        str(fuzzer),
        str(corpus),
        f"-max_total_time={budget}",
        f"-max_len={args.max_input}",
        "-timeout=25",
        "-rss_limit_mb=2048",
        "-print_final_stats=1",
        f"-artifact_prefix={artifacts}/",
    ]
    try:
        result = run(command, timeout=budget + 300)
    except subprocess.TimeoutExpired:
        print(
            f"fuzz-{args.name}=failed the fuzzer did not stop within its budget\n"
            f"reproduce: {' '.join(command)}",
            file=sys.stderr,
        )
        return 1

    if result.returncode != 0:
        found = sorted(item for item in artifacts.iterdir() if item.is_file())
        if found:
            smallest = minimize(fuzzer, found[0], work, args.name)
            print(
                f"fuzz-{args.name}=failed crash found\n"
                f"artifact: {found[0]}\n"
                f"minimized: {smallest} ({smallest.stat().st_size} bytes)\n"
                f"reproduce: {reproduce_command(fuzzer, smallest)}\n"
                f"add it to the regression corpus to keep the fix covered\n"
                f"{result.stderr[-4000:]}",
                file=sys.stderr,
            )
        else:
            print(
                f"fuzz-{args.name}=failed exit {result.returncode}\n"
                f"reproduce: {' '.join(command)}\n"
                f"{result.stderr[-4000:]}",
                file=sys.stderr,
            )
        return 1

    print(
        f"fuzz-{args.name}=ok seeds={seeds} budget={budget}s "
        f"corpus={len(list(corpus.iterdir()))}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
