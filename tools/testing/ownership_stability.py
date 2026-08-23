#!/usr/bin/env python3
"""Run the combined ownership/memory corpus repeatedly at O0..O3."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


EXPECTED_OUTPUT = "ownership-stability=ok\n1151250"
MEMORY_FAILURE_MARKERS = (
    "addresssanitizer",
    "double-free",
    "heap-use-after-free",
    "managed pointer(s) leaked",
    "memory leak detected",
)


def run(
    command: list[str],
    timeout: float,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
        # Closed standard input rather than the runner's, so a program can
        # never block on a terminal that happens to be attached.
        stdin=subprocess.DEVNULL,
        env=os.environ.copy(),
    )


def normalized_output(output: str) -> str:
    return "\n".join(output.replace("\r\n", "\n").splitlines())


def write_failure(
    root: Path,
    reason: str,
    level: int,
    run_id: int,
    command: list[str],
    result: subprocess.CompletedProcess[str] | None,
) -> None:
    details: dict[str, object] = {
        "reason": reason,
        "optimization": f"O{level}",
        "run": run_id,
        "command": command,
    }
    if result is not None:
        details.update(
            {
                "returncode": result.returncode,
                "stdout": result.stdout,
                "stderr": result.stderr,
            }
        )
    (root / "failure.json").write_text(
        json.dumps(details, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--configuration", default="unknown")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=45.0)
    args = parser.parse_args()

    compiler = args.compiler.resolve()
    source = args.source.resolve()
    if not compiler.exists():
        parser.error(f"compiler does not exist: {compiler}")
    if not source.exists():
        parser.error(f"source does not exist: {source}")
    if args.runs <= 0:
        parser.error("--runs must be positive")

    root = args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    failure = root / "failure.json"
    if failure.exists():
        failure.unlink()

    executable_suffix = ".exe" if os.name == "nt" else ""
    baseline: str | None = None

    for level in range(4):
        executable = root / f"ownership-stability-O{level}{executable_suffix}"
        build_command = [
            str(compiler),
            str(source),
            f"-O{level}",
            "--build-exe",
            "-o",
            str(executable),
        ]
        try:
            build_result = run(build_command, args.timeout)
        except subprocess.TimeoutExpired:
            write_failure(
                root, "compiler timed out", level, 0, build_command, None)
            return 1
        if build_result.returncode != 0:
            write_failure(
                root, "compiler failed", level, 0,
                build_command, build_result)
            return 1

        for run_id in range(1, args.runs + 1):
            run_command = [str(executable)]
            try:
                run_result = run(run_command, args.timeout)
            except subprocess.TimeoutExpired:
                write_failure(
                    root, "program timed out", level, run_id,
                    run_command, None)
                return 1
            combined = f"{run_result.stdout}\n{run_result.stderr}".lower()
            if run_result.returncode != 0:
                write_failure(
                    root, "program failed", level, run_id,
                    run_command, run_result)
                return 1
            if any(marker in combined for marker in MEMORY_FAILURE_MARKERS):
                write_failure(
                    root, "memory diagnostic detected", level, run_id,
                    run_command, run_result)
                return 1

            output = normalized_output(run_result.stdout)
            if output != EXPECTED_OUTPUT:
                write_failure(
                    root, "output differs from stability oracle",
                    level, run_id, run_command, run_result)
                return 1
            if baseline is None:
                baseline = output
            elif output != baseline:
                write_failure(
                    root, "optimization levels or repeated runs disagree",
                    level, run_id, run_command, run_result)
                return 1

    print(
        f"ownership-stability-differential=ok levels=4 "
        f"runs={args.runs} configuration={args.configuration}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
