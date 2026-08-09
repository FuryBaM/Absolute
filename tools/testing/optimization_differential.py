#!/usr/bin/env python3
"""Generate deterministic Absolute programs and compare O0..O3 execution."""

from __future__ import annotations

import argparse
import json
import os
import random
import subprocess
import sys
from pathlib import Path


MASK = 2_147_483_647


def mix(left: int, right: int, salt: int) -> int:
    return ((left * 17 + right * 31 + salt) ^ (left * 3 + right * 5)) & MASK


def expected_checksum(
    first: int,
    second: int,
    salt: int,
    iterations: int,
) -> int:
    left = first
    right = second
    checksum = 0
    for index in range(iterations):
        next_value = mix(left, right, salt + index)
        if (next_value & 1) == 0:
            left = (next_value + right + index) & MASK
        else:
            left = (next_value + right * 3 + index) & MASK
        right = mix(right, left, salt * 7 + index)
        checksum = (checksum + (left ^ right) + index) & MASK
    return checksum


def generate_program(
    case_id: int,
    first: int,
    second: int,
    salt: int,
    iterations: int,
    expected: int,
) -> str:
    return f"""// generated differential case {case_id}
int64 mix(int64 left, int64 right, int64 salt) {{
    return ((left * 17 + right * 31 + salt) ^
        (left * 3 + right * 5)) & 2147483647;
}}

int32 main() {{
    int64 left = {first};
    int64 right = {second};
    int64 checksum = 0;
    int32 index = 0;
    while (index < {iterations}) {{
        int64 next = mix(left, right, {salt} + index);
        if ((next & 1) == 0) {{
            left = (next + right + index) & 2147483647;
        }} else {{
            left = (next + right * 3 + index) & 2147483647;
        }}
        right = mix(right, left, {salt * 7} + index);
        checksum = (checksum + (left ^ right) + index) & 2147483647;
        index += 1;
    }}
    assert(checksum == {expected},
        "generated differential checksum");
    println(checksum);
    return 0;
}}
"""


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
        env=os.environ.copy(),
    )


def normalized_output(output: str) -> str:
    return "\n".join(output.replace("\r\n", "\n").splitlines())


def write_failure(
    root: Path,
    reason: str,
    case_id: int,
    level: int,
    command: list[str],
    result: subprocess.CompletedProcess[str] | None,
    source: Path | None = None,
    observations: list[dict[str, object]] | None = None,
) -> None:
    """Record the failing level plus the source and every level already run.

    Reporting only the level that tripped leaves out the outputs it disagreed
    with, which are exactly what identifies the divergence.
    """
    details: dict[str, object] = {
        "reason": reason,
        "case": case_id,
        "optimization": f"O{level}",
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
    if source is not None:
        details["source"] = str(source)
        try:
            details["sourceText"] = source.read_text(encoding="utf-8")
        except OSError:
            pass
    if observations:
        details["observations"] = observations
    (root / "failure.json").write_text(
        json.dumps(details, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--configuration", default="unknown")
    parser.add_argument("--seed", type=int, default=2_607_280)
    parser.add_argument("--cases", type=int, default=6)
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()

    compiler = args.compiler.resolve()
    if not compiler.exists():
        parser.error(f"compiler does not exist: {compiler}")
    if args.cases <= 0:
        parser.error("--cases must be positive")

    root = args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    failure = root / "failure.json"
    if failure.exists():
        failure.unlink()

    rng = random.Random(args.seed)
    executable_suffix = ".exe" if os.name == "nt" else ""

    for case_id in range(args.cases):
        first = rng.randint(1, 1_000_000)
        second = rng.randint(1, 1_000_000)
        salt = rng.randint(1, 100_000)
        iterations = rng.randint(24, 96)
        expected = expected_checksum(
            first, second, salt, iterations)
        source = root / f"case-{case_id:03d}.abs"
        source.write_text(
            generate_program(
                case_id, first, second, salt,
                iterations, expected),
            encoding="utf-8",
        )

        baseline: str | None = None
        # Every level already run, so a mismatch report names what the failing
        # level disagreed with rather than only itself.
        observations: list[dict[str, object]] = []
        for level in range(4):
            executable = root / (
                f"case-{case_id:03d}-O{level}{executable_suffix}")
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
                    root, "compiler timed out", case_id,
                    level, build_command, None, source, observations)
                return 1
            if build_result.returncode != 0:
                write_failure(
                    root, "compiler failed", case_id,
                    level, build_command, build_result, source, observations)
                return 1

            run_command = [str(executable)]
            try:
                run_result = run(run_command, args.timeout)
            except subprocess.TimeoutExpired:
                write_failure(
                    root, "program timed out", case_id,
                    level, run_command, None, source, observations)
                return 1
            if run_result.returncode != 0:
                write_failure(
                    root, "program failed", case_id,
                    level, run_command, run_result, source, observations)
                return 1

            output = normalized_output(run_result.stdout)
            observations.append({
                "optimization": f"O{level}",
                "runCommand": run_command,
                "returncode": run_result.returncode,
                "stdout": run_result.stdout,
                "stderr": run_result.stderr,
            })
            if output != str(expected):
                write_failure(
                    root, "output differs from generated oracle",
                    case_id, level, run_command, run_result, source,
                    observations)
                return 1
            if baseline is None:
                baseline = output
            elif output != baseline:
                write_failure(
                    root, "optimization levels disagree",
                    case_id, level, run_command, run_result, source,
                    observations)
                return 1

    print(
        f"optimization-differential=ok seed={args.seed} "
        f"cases={args.cases} levels=4 "
        f"configuration={args.configuration}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
