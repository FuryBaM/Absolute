#!/usr/bin/env python3
"""Small deterministic grammar/mutation fuzzer for absolutec.

The valid corpus must pass both parsing and semantic analysis. Mutated inputs may be
accepted or rejected, but the compiler must not crash, hang, or be killed by a signal.
Every failure is written to the reproducer directory together with stdout/stderr.
"""

from __future__ import annotations

import argparse
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path


BINARY_OPS = ["+", "-", "*", "^", "&", "|"]
COMPARE_OPS = ["<", "<=", ">", ">=", "==", "!="]
VARIABLES = ["a", "b", "c", "i"]
INSERT_TOKENS = [
    "{", "}", "(", ")", ";", ",", "@spawn", "await", "raw", "weak",
    "int32", "return", "foreach", "namespace", "\"unterminated", "/*", "*/",
    "\u03bb", "\u0000", "999999999999999999999999999999999999",
]


def expression(rng: random.Random, depth: int = 0) -> str:
    if depth >= 3 or rng.random() < 0.35:
        if rng.random() < 0.55:
            return rng.choice(VARIABLES)
        return str(rng.randint(-5000, 5000))

    kind = rng.randrange(5)
    if kind == 0:
        return f"({expression(rng, depth + 1)} {rng.choice(BINARY_OPS)} {expression(rng, depth + 1)})"
    if kind == 1:
        divisor = rng.randint(1, 97)
        return f"({expression(rng, depth + 1)} % {divisor})"
    if kind == 2:
        return f"(-{expression(rng, depth + 1)})"
    if kind == 3:
        return f"helper({expression(rng, depth + 1)}, {expression(rng, depth + 1)})"
    return f"({expression(rng, depth + 1)} + {rng.randint(0, 31)})"


def condition(rng: random.Random) -> str:
    return f"{expression(rng, 1)} {rng.choice(COMPARE_OPS)} {expression(rng, 1)}"


def valid_program(rng: random.Random, case_id: int) -> str:
    loop_limit = rng.randint(1, 12)
    a = rng.randint(-1000, 1000)
    b = rng.randint(-1000, 1000)
    c = rng.randint(-1000, 1000)
    body_lines: list[str] = []
    for _ in range(rng.randint(3, 8)):
        target = rng.choice(["a", "b", "c"])
        body_lines.append(f"        {target} = {expression(rng)};")
    body = "\n".join(body_lines)

    return f'''int32 helper(int32 x, int32 y) {{
    int32 value = (x * 17 + y * 31) ^ (x - y);
    if ((value & 1) == 0) {{
        value += 3;
    }} else {{
        value -= 5;
    }}
    return value;
}}

int32 main() {{
    int32 a = {a};
    int32 b = {b};
    int32 c = {c};
    int32 i = 0;
    while (i < {loop_limit}) {{
{body}
        if ({condition(rng)}) {{
            c += helper(a, b);
        }} else {{
            c -= helper(b, a);
        }}
        i += 1;
    }}
    int32 checksum = helper(a, b) + helper(b, c) + helper(c, a) + {case_id};
    assert(checksum == checksum, "generated checksum remains defined");
    return 0;
}}
'''


def mutate(rng: random.Random, source: str) -> str:
    data = source
    for _ in range(rng.randint(1, 5)):
        operation = rng.randrange(6)
        if operation == 0 and data:
            start = rng.randrange(len(data))
            length = rng.randint(1, min(40, len(data) - start))
            data = data[:start] + data[start + length :]
        elif operation == 1:
            position = rng.randrange(len(data) + 1)
            data = data[:position] + rng.choice(INSERT_TOKENS) + data[position:]
        elif operation == 2 and data:
            start = rng.randrange(len(data))
            end = min(len(data), start + rng.randint(1, 80))
            fragment = data[start:end]
            position = rng.randrange(len(data) + 1)
            data = data[:position] + fragment + data[position:]
        elif operation == 3:
            data = data.replace(
                rng.choice(["int32", "return", "while", "if"]),
                rng.choice(INSERT_TOKENS),
                1,
            )
        elif operation == 4:
            data = rng.choice(["}", ")", "]", "/*", "\"", "@"]) * rng.randint(1, 64) + data
        else:
            data += "\n" + rng.choice(INSERT_TOKENS) * rng.randint(1, 32)
    return data


def write_failure(
    directory: Path,
    name: str,
    source: str,
    command: list[str],
    result: subprocess.CompletedProcess[str] | None,
    reason: str,
) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    (directory / f"{name}.abs").write_text(
        source, encoding="utf-8", errors="surrogatepass"
    )
    details = [reason, "command: " + " ".join(command)]
    if result is not None:
        details.extend(
            [
                f"returncode: {result.returncode}",
                "stdout:",
                result.stdout,
                "stderr:",
                result.stderr,
            ]
        )
    (directory / f"{name}.txt").write_text("\n".join(details), encoding="utf-8")


def run_compiler(command: list[str], timeout: float) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env.setdefault("ASAN_OPTIONS", "abort_on_error=1:detect_leaks=1")
    env.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
    return subprocess.run(
        command,
        text=True,
        capture_output=True,
        timeout=timeout,
        env=env,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--seed", type=int, default=689409)
    parser.add_argument("--valid-cases", type=int, default=75)
    parser.add_argument("--mutated-cases", type=int, default=225)
    parser.add_argument("--timeout", type=float, default=4.0)
    parser.add_argument("--repro-dir", type=Path, default=Path("fuzz-reproducers"))
    args = parser.parse_args()

    compiler = args.compiler.resolve()
    if not compiler.exists():
        parser.error(f"compiler does not exist: {compiler}")

    rng = random.Random(args.seed)
    args.repro_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="absolute-fuzz-") as temporary:
        root = Path(temporary)

        for case_id in range(args.valid_cases):
            source = valid_program(rng, case_id)
            path = root / f"valid-{case_id:05d}.abs"
            path.write_text(source, encoding="utf-8")
            for phase, extra in (("parse", ["--parse-only"]), ("analyze", [])):
                command = [str(compiler), str(path), *extra]
                try:
                    result = run_compiler(command, args.timeout)
                except subprocess.TimeoutExpired:
                    write_failure(
                        args.repro_dir,
                        f"valid-{case_id:05d}-{phase}",
                        source,
                        command,
                        None,
                        "compiler timed out on valid generated program",
                    )
                    return 1
                if result.returncode != 0:
                    write_failure(
                        args.repro_dir,
                        f"valid-{case_id:05d}-{phase}",
                        source,
                        command,
                        result,
                        "compiler rejected valid generated program",
                    )
                    return 1

        for case_id in range(args.mutated_cases):
            source = mutate(rng, valid_program(rng, case_id))
            path = root / f"mutated-{case_id:05d}.abs"
            path.write_text(source, encoding="utf-8", errors="surrogatepass")
            command = [str(compiler), str(path), "--parse-only"]
            try:
                result = run_compiler(command, args.timeout)
            except subprocess.TimeoutExpired:
                write_failure(
                    args.repro_dir,
                    f"mutated-{case_id:05d}",
                    source,
                    command,
                    None,
                    "compiler timed out on mutated input",
                )
                return 1

            # Diagnostics are expected for malformed inputs. Signals and common
            # shell crash encodings are not.
            if result.returncode < 0 or result.returncode in {
                128,
                132,
                133,
                134,
                136,
                137,
                139,
            }:
                write_failure(
                    args.repro_dir,
                    f"mutated-{case_id:05d}",
                    source,
                    command,
                    result,
                    "compiler crashed on mutated input",
                )
                return 1

    print(
        f"compiler-fuzz=ok seed={args.seed} valid={args.valid_cases} "
        f"mutated={args.mutated_cases}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
