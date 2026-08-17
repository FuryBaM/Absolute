#!/usr/bin/env python3
"""Compare native and WebAssembly execution for one deterministic corpus."""

from __future__ import annotations

import argparse
import json
import os
import random
import subprocess
import sys
from pathlib import Path

from optimization_differential import (
    build_case,
    normalized_output,
)


WASM_RUNNER = """'use strict';
const fs = require('fs');
const { instantiateAbsoluteWasm } = require(process.argv[2]);

async function main() {
    const bytes = fs.readFileSync(process.argv[3]);
    const session = await instantiateAbsoluteWasm(
        bytes, { captureLogs: true, allowNetwork: false });
    try {
        if (typeof session.exports.main !== 'function') {
            throw new Error('main export missing');
        }
        const code = session.exports.main();
        if (code !== 0) {
            throw new Error(`main returned ${code}`);
        }
        process.stdout.write(session.logs.join(''));
    } finally {
        try { session.host.shutdown(); } catch (_) {}
    }
}

main().catch((error) => {
    console.error(error);
    process.exit(1);
});
"""


def run(
    command: list[str],
    timeout: float,
    wasm_ld: Path,
) -> subprocess.CompletedProcess[str]:
    environment = os.environ.copy()
    environment["ABSOLUTE_WASM_LD"] = str(wasm_ld)
    return subprocess.run(
        command,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
        env=environment,
    )


def write_failure(
    root: Path,
    reason: str,
    case_id: int,
    backend: str,
    command: list[str],
    result: subprocess.CompletedProcess[str] | None,
    native_output: str | None = None,
) -> None:
    details: dict[str, object] = {
        "reason": reason,
        "case": case_id,
        "backend": backend,
        "command": command,
    }
    if native_output is not None:
        details["native_output"] = native_output
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


def checked_run(
    root: Path,
    command: list[str],
    timeout: float,
    wasm_ld: Path,
    reason: str,
    case_id: int,
    backend: str,
) -> subprocess.CompletedProcess[str] | None:
    try:
        result = run(command, timeout, wasm_ld)
    except subprocess.TimeoutExpired:
        write_failure(
            root, f"{reason} timed out", case_id,
            backend, command, None)
        return None
    if result.returncode != 0:
        write_failure(
            root, f"{reason} failed", case_id,
            backend, command, result)
        return None
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--node", required=True, type=Path)
    parser.add_argument("--wasm-host", required=True, type=Path)
    parser.add_argument("--wasm-ld", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--configuration", default="unknown")
    parser.add_argument("--seed", type=int, default=2_607_280)
    parser.add_argument("--cases", type=int, default=6)
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()

    compiler = args.compiler.resolve()
    node = args.node.resolve()
    wasm_host = args.wasm_host.resolve()
    wasm_ld = args.wasm_ld.resolve()
    for name, path in (
        ("compiler", compiler),
        ("node", node),
        ("wasm host", wasm_host),
        ("wasm-ld", wasm_ld),
    ):
        if not path.exists():
            parser.error(f"{name} does not exist: {path}")
    if args.cases <= 0:
        parser.error("--cases must be positive")

    root = args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    failure = root / "failure.json"
    if failure.exists():
        failure.unlink()
    runner = root / "run-absolute-wasm.js"
    runner.write_text(WASM_RUNNER, encoding="utf-8")

    rng = random.Random(args.seed)
    executable_suffix = ".exe" if os.name == "nt" else ""

    for case_id in range(args.cases):
        source_text, expected = build_case(rng, case_id)
        source = root / f"case-{case_id:03d}.abs"
        source.write_text(source_text, encoding="utf-8")

        native = root / (
            f"case-{case_id:03d}-native{executable_suffix}")
        native_build = [
            str(compiler), str(source), "-O3",
            "--build-exe", "-o", str(native),
        ]
        if checked_run(
            root, native_build, args.timeout, wasm_ld,
            "native compilation", case_id, "native") is None:
            return 1
        native_result = checked_run(
            root, [str(native)], args.timeout, wasm_ld,
            "native execution", case_id, "native")
        if native_result is None:
            return 1
        native_output = normalized_output(native_result.stdout)
        if expected is not None and native_output != str(expected):
            write_failure(
                root, "native output differs from generated oracle",
                case_id, "native", [str(native)],
                native_result)
            return 1

        wasm = root / f"case-{case_id:03d}.wasm"
        wasm_build = [
            str(compiler), str(source), "-O3",
            "--target", "wasm32-unknown-unknown",
            "--build-exe", "-o", str(wasm),
        ]
        if checked_run(
            root, wasm_build, args.timeout, wasm_ld,
            "WebAssembly compilation", case_id, "wasm") is None:
            return 1
        wasm_run = [
            str(node), str(runner), str(wasm_host), str(wasm),
        ]
        wasm_result = checked_run(
            root, wasm_run, args.timeout, wasm_ld,
            "WebAssembly execution", case_id, "wasm")
        if wasm_result is None:
            return 1
        wasm_output = normalized_output(wasm_result.stdout)
        if expected is not None and wasm_output != str(expected):
            write_failure(
                root, "WebAssembly output differs from generated oracle",
                case_id, "wasm", wasm_run, wasm_result,
                native_output)
            return 1
        if wasm_output != native_output:
            write_failure(
                root, "native and WebAssembly outputs disagree",
                case_id, "wasm", wasm_run, wasm_result,
                native_output)
            return 1

    print(
        f"native-wasm-differential=ok seed={args.seed} "
        f"cases={args.cases} configuration={args.configuration}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
