#!/usr/bin/env python3
"""Run one ABI corpus through a native or WebAssembly linker."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any


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


def normalized_output(output: str) -> str:
    return "\n".join(output.replace("\r\n", "\n").splitlines())


def detect_format(path: Path) -> str:
    with path.open("rb") as stream:
        header = stream.read(8)
    if header.startswith(b"MZ"):
        return "pe"
    if header.startswith(b"\x7fELF"):
        return "elf"
    if header.startswith(b"\x00asm"):
        return "wasm"
    if len(header) >= 4 and header[:4] in (
        b"\xfe\xed\xfa\xce",
        b"\xce\xfa\xed\xfe",
        b"\xfe\xed\xfa\xcf",
        b"\xcf\xfa\xed\xfe",
        b"\xca\xfe\xba\xbe",
    ):
        return "macho"
    return "unknown"


def execute(
    command: list[str],
    timeout: float,
    wasm_ld: Path | None,
) -> subprocess.CompletedProcess[str]:
    environment = os.environ.copy()
    if wasm_ld is not None:
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
    case_name: str,
    backend_label: str,
    command: list[str],
    result: subprocess.CompletedProcess[str] | None = None,
    **details: Any,
) -> None:
    report: dict[str, Any] = {
        "reason": reason,
        "case": case_name,
        "backend": backend_label,
        "command": command,
        **details,
    }
    if result is not None:
        report.update(
            {
                "returncode": result.returncode,
                "stdout": result.stdout,
                "stderr": result.stderr,
            }
        )
    (root / "failure.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )


def checked_execute(
    root: Path,
    command: list[str],
    timeout: float,
    wasm_ld: Path | None,
    action: str,
    case_name: str,
    backend_label: str,
) -> subprocess.CompletedProcess[str] | None:
    try:
        result = execute(command, timeout, wasm_ld)
    except subprocess.TimeoutExpired:
        write_failure(
            root,
            f"{action} timed out",
            case_name,
            backend_label,
            command,
        )
        return None
    if result.returncode != 0:
        write_failure(
            root,
            f"{action} failed",
            case_name,
            backend_label,
            command,
            result,
        )
        return None
    return result


def load_corpus(manifest: Path) -> tuple[Path, list[dict[str, str]]]:
    data = json.loads(manifest.read_text(encoding="utf-8"))
    if data.get("version") != 1:
        raise ValueError("ABI linker corpus version must be 1")
    raw_cases = data.get("cases")
    if not isinstance(raw_cases, list) or not raw_cases:
        raise ValueError("ABI linker corpus must contain cases")

    repository = manifest.parent.parent
    cases: list[dict[str, str]] = []
    names: set[str] = set()
    for index, item in enumerate(raw_cases):
        if not isinstance(item, dict):
            raise ValueError(f"case {index} must be an object")
        name = item.get("name")
        source = item.get("source")
        expected = item.get("stdout")
        if not all(isinstance(value, str) and value for value in (
            name, source, expected
        )):
            raise ValueError(
                f"case {index} requires non-empty name/source/stdout")
        if name in names:
            raise ValueError(f"duplicate ABI corpus case: {name}")
        source_path = (repository / source).resolve()
        if not source_path.is_file():
            raise ValueError(
                f"ABI corpus source does not exist: {source_path}")
        names.add(name)
        cases.append(
            {
                "name": name,
                "source": str(source_path),
                "stdout": expected,
            }
        )
    return repository, cases


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--corpus", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument(
        "--backend", required=True, choices=("native", "wasm"))
    parser.add_argument("--backend-label", required=True)
    parser.add_argument(
        "--expected-format",
        required=True,
        choices=("pe", "elf", "macho", "wasm"),
    )
    parser.add_argument("--node", type=Path)
    parser.add_argument("--wasm-host", type=Path)
    parser.add_argument("--wasm-ld", type=Path)
    parser.add_argument("--optimization", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=45.0)
    args = parser.parse_args()

    compiler = args.compiler.resolve()
    manifest = args.corpus.resolve()
    if not compiler.is_file():
        parser.error(f"compiler does not exist: {compiler}")
    if not manifest.is_file():
        parser.error(f"corpus does not exist: {manifest}")
    if args.optimization not in range(4):
        parser.error("--optimization must be between 0 and 3")

    node: Path | None = None
    wasm_host: Path | None = None
    wasm_ld: Path | None = None
    if args.backend == "wasm":
        missing = [
            name
            for name, value in (
                ("--node", args.node),
                ("--wasm-host", args.wasm_host),
                ("--wasm-ld", args.wasm_ld),
            )
            if value is None
        ]
        if missing:
            parser.error(
                "WebAssembly backend requires " + ", ".join(missing))
        node = args.node.resolve()
        wasm_host = args.wasm_host.resolve()
        wasm_ld = args.wasm_ld.resolve()
        for name, path in (
            ("node", node),
            ("wasm host", wasm_host),
            ("wasm-ld", wasm_ld),
        ):
            if not path.is_file():
                parser.error(f"{name} does not exist: {path}")

    try:
        _, cases = load_corpus(manifest)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))

    root = args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    for report_name in ("failure.json", "results.json"):
        report = root / report_name
        if report.exists():
            report.unlink()

    runner: Path | None = None
    if args.backend == "wasm":
        runner = root / "run-absolute-wasm.js"
        runner.write_text(WASM_RUNNER, encoding="utf-8")

    executable_suffix = ".exe" if os.name == "nt" else ""
    results: list[dict[str, str]] = []
    for case in cases:
        name = case["name"]
        source = Path(case["source"])
        if args.backend == "native":
            artifact = root / f"{name}{executable_suffix}"
            build_command = [
                str(compiler),
                str(source),
                f"-O{args.optimization}",
                "--build-exe",
                "-o",
                str(artifact),
            ]
        else:
            artifact = root / f"{name}.wasm"
            build_command = [
                str(compiler),
                str(source),
                f"-O{args.optimization}",
                "--target",
                "wasm32-unknown-unknown",
                "--build-exe",
                "-o",
                str(artifact),
            ]

        build_result = checked_execute(
            root,
            build_command,
            args.timeout,
            wasm_ld,
            "link",
            name,
            args.backend_label,
        )
        if build_result is None:
            return 1
        if not artifact.is_file():
            write_failure(
                root,
                "linker did not produce an artifact",
                name,
                args.backend_label,
                build_command,
            )
            return 1

        artifact_format = detect_format(artifact)
        if artifact_format != args.expected_format:
            write_failure(
                root,
                "linked artifact format differs from contract",
                name,
                args.backend_label,
                build_command,
                expected_format=args.expected_format,
                actual_format=artifact_format,
            )
            return 1

        if args.backend == "native":
            run_command = [str(artifact)]
        else:
            assert runner is not None
            assert node is not None
            assert wasm_host is not None
            run_command = [
                str(node),
                str(runner),
                str(wasm_host),
                str(artifact),
            ]
        run_result = checked_execute(
            root,
            run_command,
            args.timeout,
            wasm_ld,
            "execution",
            name,
            args.backend_label,
        )
        if run_result is None:
            return 1

        actual_output = normalized_output(run_result.stdout)
        if actual_output != case["stdout"]:
            write_failure(
                root,
                "stdout differs from ABI corpus oracle",
                name,
                args.backend_label,
                run_command,
                run_result,
                expected_stdout=case["stdout"],
                actual_stdout=actual_output,
                artifact_format=artifact_format,
            )
            return 1

        results.append(
            {
                "case": name,
                "source": str(source),
                "stdout": actual_output,
                "format": artifact_format,
            }
        )

    (root / "results.json").write_text(
        json.dumps(
            {
                "corpusVersion": 1,
                "backend": args.backend_label,
                "optimization": f"O{args.optimization}",
                "cases": results,
            },
            indent=2,
            ensure_ascii=False,
        ),
        encoding="utf-8",
    )
    print(
        f"abi-linker-corpus=ok backend={args.backend_label} "
        f"format={args.expected_format} cases={len(results)}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
