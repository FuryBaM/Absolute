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



# Shapes below print a result instead of asserting a precomputed one. The
# oracle for them is agreement: every optimization level, and native against
# WebAssembly, must produce identical output. That deliberately cannot catch a
# wrong answer produced identically everywhere, which is what the checksum
# shape above is for; it can catch the divergence this corpus exists to find,
# without a Python model of Absolute's semantics standing in as a second
# implementation that could itself be wrong.

def generate_float_program(case_id: int, first: int, second: int,
                           iterations: int) -> str:
    """Double arithmetic, comparison and conversion.

    Floating point is where native and WebAssembly are most likely to part
    company: excess intermediate precision, contraction into fused multiply-add
    and differing rounding of int/double conversion all show up here and
    nowhere in the integer shape. The accumulator is scaled back to an integer
    before printing so the comparison stays exact.
    """
    return f"""// generated differential case {case_id} (float)
double blend(double left, double right, double weight) {{
    double scaled = left * weight + right * (1.0 - weight);
    if (scaled > 1000000.0) {{
        return scaled / 3.0;
    }}
    return scaled * 1.5;
}}

int32 main() {{
    double left = {first}.0;
    double right = {second}.0;
    double total = 0.0;
    int32 index = 0;
    while (index < {iterations}) {{
        double weight = (index as double) / {iterations}.0;
        double mixed = blend(left, right, weight);
        left = right / 2.0 + 1.0;
        right = mixed / 4.0 + 1.0;
        if (mixed > total) {{
            total = total + mixed / 1000.0;
        }} else {{
            total = total + 1.0;
        }}
        index += 1;
    }}
    println((total * 1000.0) as int64);
    return 0;
}}
"""


def generate_integer_edge_program(case_id: int, first: int, second: int,
                                  iterations: int) -> str:
    """Division, remainder, shifts and width conversion.

    The checksum shape stays inside multiplication, xor and mask, so it never
    reaches the integer operations whose edges are defined differently across
    backends: truncation direction of division and remainder with a negative
    operand, shift amounts approaching the width, and narrowing between int64
    and int32.
    """
    return f"""// generated differential case {case_id} (integer edges)
int64 fold(int64 value, int64 step) {{
    int64 shifted = value << (step & 15);
    int64 divided = value / (step + 1);
    return shifted + divided - (value % (step + 3));
}}

int32 main() {{
    int64 accumulator = {first};
    int64 negative = -{second};
    int32 narrow = 0;
    int32 index = 0;
    while (index < {iterations}) {{
        int64 step = (index as int64) + 1;
        accumulator = fold(accumulator, step) & 4611686018427387903;
        negative = negative / (step + 2) - negative % (step + 5);
        narrow = (accumulator as int32) + narrow;
        index += 1;
    }}
    println(accumulator);
    println(negative);
    println(narrow as int64);
    return 0;
}}
"""


def build_case(rng: random.Random, case_id: int) -> tuple[str, int | None]:
    """Pick the shape for one case and render it.

    Shared by the optimization-level and the native/WebAssembly runners so the
    two corpora stay the same corpus; a shape added here is exercised by both
    without touching either loop. The second element is the precomputed oracle
    where one exists, and None for the shapes whose only oracle is agreement
    between configurations.
    """
    first = rng.randint(1, 1_000_000)
    second = rng.randint(1, 1_000_000)
    salt = rng.randint(1, 100_000)
    iterations = rng.randint(24, 96)
    shape = case_id % 3
    if shape == 1:
        return generate_float_program(case_id, first, second, iterations), None
    if shape == 2:
        return (generate_integer_edge_program(
            case_id, first, second, iterations), None)
    expected = expected_checksum(first, second, salt, iterations)
    return (generate_program(case_id, first, second, salt,
                             iterations, expected), expected)


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
        source_text, expected = build_case(rng, case_id)
        source = root / f"case-{case_id:03d}.abs"
        source.write_text(source_text, encoding="utf-8")

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
            if expected is not None and output != str(expected):
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
