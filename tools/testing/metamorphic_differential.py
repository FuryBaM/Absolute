#!/usr/bin/env python3
"""Compare semantics-preserving variants of the same Absolute program.

A metamorphic test does not need an oracle for the value a program computes:
it only needs two programs that must agree. Each case here is emitted in
several shapes that a correct compiler has to treat identically:

* alpha-renaming — every declared name is substituted, nothing else changes;
* dead code — an uncalled function plus a branch guarded by a condition that
  is false for every reachable state;
* declaration order — independent top-level declarations are permuted;
* constant versus runtime operands — a literal is replaced by a value the
  program computes, so constant folding and the runtime path must agree.

Variants are generated from one template rather than rewritten textually, so
a transformation cannot accidentally change meaning. When outputs disagree
the case is minimized and every variant's source, command and output is
written to failure.json.
"""

from __future__ import annotations

import argparse
import json
import os
import random
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

MASK = 2_147_483_647

BASE_NAMES = {
    "mix": "mix",
    "seed": "seed",
    "unused": "unused",
    "left": "left",
    "right": "right",
    "salt": "salt",
    "checksum": "checksum",
    "index": "index",
    "next": "next",
}

RENAMED = {
    "mix": "blend",
    "seed": "origin",
    "unused": "spare",
    "left": "alpha",
    "right": "beta",
    "salt": "pepper",
    "checksum": "digest",
    "index": "cursor",
    "next": "candidate",
}


@dataclass(frozen=True)
class Case:
    """The parameters of one generated program."""
    identifier: int
    first: int
    second: int
    salt: int
    iterations: int


@dataclass(frozen=True)
class Shape:
    """Which semantics-preserving transformations to apply."""
    name: str
    names: dict = field(default_factory=lambda: BASE_NAMES)
    dead_code: bool = False
    helper_last: bool = False
    runtime_operands: bool = False


SHAPES = (
    Shape("baseline"),
    Shape("alpha-renamed", names=RENAMED),
    Shape("dead-code", dead_code=True),
    Shape("reordered", helper_last=True),
    Shape("runtime-operands", runtime_operands=True),
    # Applying everything at once catches a transformation that is only
    # mishandled in combination with another.
    Shape("combined", names=RENAMED, dead_code=True, helper_last=True,
          runtime_operands=True),
)


def expected_checksum(case: Case) -> int:
    left = case.first
    right = case.second
    checksum = 0
    for index in range(case.iterations):
        mixed = ((left * 17 + right * 31 + case.salt + index) ^
                 (left * 3 + right * 5)) & MASK
        if (mixed & 1) == 0:
            left = (mixed + right + index) & MASK
        else:
            left = (mixed + right * 3 + index) & MASK
        right = ((right * 17 + left * 31 + case.salt * 7 + index) ^
                 (right * 3 + left * 5)) & MASK
        checksum = (checksum + (left ^ right) + index) & MASK
    return checksum


def helper_text(shape: Shape) -> str:
    name = shape.names
    return f"""int64 {name['mix']}(int64 {name['left']}, int64 {name['right']}, int64 {name['salt']}) {{
    return (({name['left']} * 17 + {name['right']} * 31 + {name['salt']}) ^
        ({name['left']} * 3 + {name['right']} * 5)) & {MASK};
}}
"""


def dead_helper_text(shape: Shape) -> str:
    if not shape.dead_code:
        return ""
    name = shape.names
    # Never called. A correct compiler may discard it, but discarding it must
    # not change what the rest of the program prints.
    return f"""int64 {name['unused']}(int64 {name['left']}) {{
    return {name['left']} * 3 + 1;
}}
"""


def seed_helpers_text(shape: Shape, case: Case) -> str:
    if not shape.runtime_operands:
        return ""
    name = shape.names
    # Returns its argument. The call sites below pass the same literals the
    # constant shape uses, so the operands are equal but no longer literals.
    return f"""int64 {name['seed']}(int64 {name['left']}) {{
    return {name['left']};
}}
"""


def operand(shape: Shape, value: int) -> str:
    if not shape.runtime_operands:
        return str(value)
    return f"{shape.names['seed']}({value})"


def main_text(shape: Shape, case: Case, expected: int) -> str:
    name = shape.names
    dead_branch = ""
    if shape.dead_code:
        # checksum is masked into [0, MASK], so this never runs. If it ever
        # did, the extra line would show up as an output mismatch.
        dead_branch = f"""    if ({name['checksum']} < 0) {{
        println({name['unused']}({name['checksum']}));
    }}
"""
    return f"""int32 main() {{
    int64 {name['left']} = {operand(shape, case.first)};
    int64 {name['right']} = {operand(shape, case.second)};
    int64 {name['checksum']} = 0;
    int32 {name['index']} = 0;
    while ({name['index']} < {operand(shape, case.iterations)}) {{
        int64 {name['next']} = {name['mix']}({name['left']}, {name['right']},
            {operand(shape, case.salt)} + {name['index']});
        if (({name['next']} & 1) == 0) {{
            {name['left']} = ({name['next']} + {name['right']} + {name['index']}) & {MASK};
        }} else {{
            {name['left']} = ({name['next']} + {name['right']} * 3 + {name['index']}) & {MASK};
        }}
        {name['right']} = {name['mix']}({name['right']}, {name['left']},
            {operand(shape, case.salt * 7)} + {name['index']});
        {name['checksum']} = ({name['checksum']} + ({name['left']} ^ {name['right']}) +
            {name['index']}) & {MASK};
        {name['index']} += 1;
    }}
{dead_branch}    assert({name['checksum']} == {expected}, "metamorphic checksum");
    println({name['checksum']});
    return 0;
}}
"""


def render(shape: Shape, case: Case, expected: int) -> str:
    helpers = helper_text(shape) + dead_helper_text(shape) + \
        seed_helpers_text(shape, case)
    body = main_text(shape, case, expected)
    header = f"// metamorphic case {case.identifier} shape {shape.name}\n"
    if shape.helper_last:
        # Calling a function declared later must work; the declaration pass
        # runs before bodies are resolved.
        return header + body + "\n" + helpers
    return header + helpers + "\n" + body


def run(command: list[str], timeout: float) -> subprocess.CompletedProcess[str]:
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


@dataclass
class Observation:
    shape: str
    source: str
    build_command: list[str]
    run_command: list[str] | None = None
    returncode: int | None = None
    stdout: str = ""
    stderr: str = ""


def write_failure(
    root: Path,
    reason: str,
    case: Case,
    expected: int,
    observations: list[Observation],
    minimized: Case | None,
) -> None:
    """Record the case, the reduced case and every variant that ran."""
    details: dict[str, object] = {
        "reason": reason,
        "case": {
            "id": case.identifier,
            "first": case.first,
            "second": case.second,
            "salt": case.salt,
            "iterations": case.iterations,
        },
        "expected": expected,
        "observations": [
            {
                "shape": item.shape,
                "source": item.source,
                "buildCommand": item.build_command,
                "runCommand": item.run_command,
                "returncode": item.returncode,
                "stdout": item.stdout,
                "stderr": item.stderr,
            }
            for item in observations
        ],
    }
    if minimized is not None:
        details["minimized"] = {
            "first": minimized.first,
            "second": minimized.second,
            "salt": minimized.salt,
            "iterations": minimized.iterations,
        }
    (root / "failure.json").write_text(
        json.dumps(details, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )


def evaluate(
    case: Case,
    root: Path,
    compiler: Path,
    timeout: float,
    suffix: str,
    collect: list[Observation] | None = None,
) -> tuple[bool, str]:
    """Build and run every shape. Returns (all shapes agreed, reason)."""
    expected = expected_checksum(case)
    baseline: str | None = None
    for shape in SHAPES:
        source = root / f"case-{case.identifier:03d}-{shape.name}.abs"
        source.write_text(render(shape, case, expected), encoding="utf-8")
        executable = root / f"case-{case.identifier:03d}-{shape.name}{suffix}"
        build_command = [
            str(compiler), str(source), "-O2", "--build-exe",
            "-o", str(executable),
        ]
        observation = Observation(shape.name, str(source), build_command)
        if collect is not None:
            collect.append(observation)

        try:
            build = run(build_command, timeout)
        except subprocess.TimeoutExpired:
            return False, f"compiler timed out on shape {shape.name}"
        observation.returncode = build.returncode
        observation.stdout = build.stdout
        observation.stderr = build.stderr
        if build.returncode != 0:
            return False, f"compiler failed on shape {shape.name}"

        run_command = [str(executable)]
        observation.run_command = run_command
        try:
            executed = run(run_command, timeout)
        except subprocess.TimeoutExpired:
            return False, f"program timed out on shape {shape.name}"
        observation.returncode = executed.returncode
        observation.stdout = executed.stdout
        observation.stderr = executed.stderr
        if executed.returncode != 0:
            return False, f"program failed on shape {shape.name}"

        output = normalized_output(executed.stdout)
        if output != str(expected):
            return False, f"shape {shape.name} disagrees with the oracle"
        if baseline is None:
            baseline = output
        elif output != baseline:
            return False, f"shape {shape.name} disagrees with baseline"
    return True, ""


def minimize(
    case: Case,
    root: Path,
    compiler: Path,
    timeout: float,
    suffix: str,
) -> Case:
    """Shrink the iteration count while the disagreement survives."""
    best = case
    iterations = case.iterations
    while iterations > 1:
        candidate = Case(case.identifier, case.first, case.second,
                         case.salt, iterations // 2)
        agreed, _ = evaluate(candidate, root, compiler, timeout, suffix)
        if agreed:
            break
        best = candidate
        iterations = candidate.iterations
    return best


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--configuration", default="unknown")
    parser.add_argument("--seed", type=int, default=8_311_907)
    parser.add_argument("--cases", type=int, default=4)
    parser.add_argument("--timeout", type=float, default=60.0)
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
    suffix = ".exe" if os.name == "nt" else ""

    for identifier in range(args.cases):
        case = Case(
            identifier,
            rng.randint(1, 1_000_000),
            rng.randint(1, 1_000_000),
            rng.randint(1, 100_000),
            rng.randint(24, 96),
        )
        observations: list[Observation] = []
        agreed, reason = evaluate(
            case, root, compiler, args.timeout, suffix, observations)
        if not agreed:
            minimized = minimize(case, root, compiler, args.timeout, suffix)
            write_failure(root, reason, case, expected_checksum(case),
                          observations, minimized)
            print(f"metamorphic-differential=failed {reason}", file=sys.stderr)
            return 1

    print(
        f"metamorphic-differential=ok shapes={len(SHAPES)} "
        f"cases={args.cases} configuration={args.configuration}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
