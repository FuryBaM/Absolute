#!/usr/bin/env python3
from __future__ import annotations

import csv
import json
import math
import os
import platform
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent
ALGORITHMS = [
    ("matrix", "Dense matrix multiplication"),
    ("dijkstra", "Dense-graph Dijkstra"),
    ("alignment", "Needleman-Wunsch alignment"),
    ("nbody", "N-body simulation"),
    ("mandelbrot", "Mandelbrot iteration"),
    ("ntt", "Number-theoretic transform"),
    ("hashjoin", "Open-addressed hash join"),
    ("raytrace", "Ray-sphere intersections"),
]
LOOPS = {
    "matrix": 20,
    "dijkstra": 25,
    "alignment": 20,
    "nbody": 20,
    "mandelbrot": 15,
    "ntt": 6,
    "hashjoin": 3,
    "raytrace": 20,
}


@dataclass(frozen=True)
class Language:
    name: str
    command: tuple[str, ...]
    samples: int
    loops_from_cli: bool = True


def languages() -> list[Language]:
    result: list[Language] = []
    absolute_dir = Path(os.environ.get("ABSOLUTE_BENCH_BIN_DIR", ROOT / "bin"))
    if all((absolute_dir / f"absolute-{name}").exists() for name, _ in ALGORITHMS):
        result.append(Language("Absolute", ("{absolute}",), 3, False))
    if (ROOT / "benchmark-cpp").exists():
        result.append(Language("C++", (str(ROOT / "benchmark-cpp"), "{algorithm}", "{loops}"), 3))
    if (ROOT / "benchmark-rust").exists():
        result.append(Language("Rust", (str(ROOT / "benchmark-rust"), "{algorithm}", "{loops}"), 3))
    if (ROOT / "benchmark-go").exists():
        result.append(Language("Go", (str(ROOT / "benchmark-go"), "{algorithm}", "{loops}"), 3))
    if (ROOT / "Benchmark.class").exists():
        result.append(Language("Java", ("java", "-Xms512m", "-Xmx512m", "-XX:+UseSerialGC", "-cp", str(ROOT), "Benchmark", "{algorithm}", "{loops}"), 3))
    return result


def build_command(language: Language, algorithm: str) -> list[str]:
    loops = LOOPS[algorithm]
    absolute = Path(os.environ.get("ABSOLUTE_BENCH_BIN_DIR", ROOT / "bin")) / f"absolute-{algorithm}"
    return [part.format(algorithm=algorithm, loops=loops, absolute=absolute) for part in language.command]


def run_once(command: list[str], expected: int | None, timeout: int = 60) -> tuple[float, int]:
    start = time.perf_counter_ns()
    completed = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=timeout)
    elapsed_ms = (time.perf_counter_ns() - start) / 1_000_000.0
    if completed.returncode != 0:
        raise RuntimeError(f"command failed ({completed.returncode}): {' '.join(command)}\n{completed.stdout}\n{completed.stderr}")
    lines = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
    if not lines:
        raise RuntimeError(f"command produced no checksum: {' '.join(command)}")
    try:
        checksum = int(lines[-1])
    except ValueError as exc:
        raise RuntimeError(f"invalid checksum from {' '.join(command)}: {lines[-1]!r}") from exc
    if expected is not None and checksum != expected:
        raise RuntimeError(f"checksum mismatch for {' '.join(command)}: expected {expected}, got {checksum}")
    return elapsed_ms, checksum


def version(command: list[str]) -> str:
    try:
        completed = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=30)
        text = (completed.stdout or completed.stderr).strip().splitlines()
        return text[0] if text else "unknown"
    except Exception:
        return "unavailable"


def main() -> int:
    selected = languages()
    if len(selected) < 2:
        raise RuntimeError("not enough benchmark implementations were built")

    rows: list[dict[str, object]] = []
    tasks = [(algorithm, description, language) for algorithm, description in ALGORITHMS for language in selected]

    cpp = next((language for language in selected if language.name == "C++"), None)
    if cpp is None:
        raise RuntimeError("C++ reference implementation is required for checksum validation")
    expected_checksums: dict[tuple[str, int], int] = {}
    for algorithm, _ in ALGORITHMS:
        loops = LOOPS[algorithm]
        _, checksum = run_once([str(ROOT / "benchmark-cpp"), algorithm, str(loops)], None)
        expected_checksums[(algorithm, loops)] = checksum

    measurements: dict[tuple[str, str], list[float]] = {}
    for algorithm, description, language in tasks:
        loops = LOOPS[algorithm]
        expected = expected_checksums[(algorithm, loops)]
        command = build_command(language, algorithm)
        samples: list[float] = []
        for sample_index in range(language.samples):
            elapsed_ms, _ = run_once(command, expected)
            samples.append(elapsed_ms / loops)
            print(f"{algorithm:10s} {language.name:10s} sample {sample_index + 1}/{language.samples}: {samples[-1]:.6f} ms/iteration", flush=True)
        measurements[(algorithm, language.name)] = samples

    language_names = [language.name for language in selected]
    medians: dict[tuple[str, str], float] = {}
    for algorithm, description in ALGORITHMS:
        for language in selected:
            samples = measurements[(algorithm, language.name)]
            median = statistics.median(samples)
            medians[(algorithm, language.name)] = median
            rows.append({
                "algorithm": algorithm,
                "description": description,
                "language": language.name,
                "median_ms": median,
                "samples": language.samples,
                "loops_per_process": LOOPS[algorithm],
                "validated_checksum": expected_checksums[(algorithm, LOOPS[algorithm])],
            })

    baseline_name = "Absolute" if "Absolute" in language_names else "C++"
    ratios: dict[tuple[str, str], float] = {}
    geomeans: dict[str, float] = {}
    for algorithm, _ in ALGORITHMS:
        baseline = medians[(algorithm, baseline_name)]
        for language_name in language_names:
            ratios[(algorithm, language_name)] = medians[(algorithm, language_name)] / baseline
    for language_name in language_names:
        geomeans[language_name] = math.exp(statistics.mean(math.log(ratios[(algorithm, language_name)]) for algorithm, _ in ALGORITHMS))

    with (ROOT / "results.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0].keys()) + ["relative_to_baseline"])
        writer.writeheader()
        for row in rows:
            writer.writerow({**row, "relative_to_baseline": ratios[(str(row["algorithm"]), str(row["language"]))]})

    metadata = {
        "platform": platform.platform(),
        "machine": platform.machine(),
        "processor": platform.processor(),
        "python": version([sys.executable, "--version"]),
        "cpp": version(["clang++", "--version"]),
        "rust": version(["rustc", "--version"]),
        "go": version(["go", "version"]),
        "java": version(["java", "-version"]),
        "baseline": baseline_name,
    }
    (ROOT / "metadata.json").write_text(json.dumps(metadata, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    lines = [
        "# Complex algorithm benchmark",
        "",
        f"Median wall-clock time per kernel iteration in milliseconds. Baseline for relative values: **{baseline_name}**.",
        "Each process executes multiple identical kernel iterations; the reported time is normalized per iteration.",
        "",
        "| Algorithm | " + " | ".join(language_names) + " |",
        "|---|" + "---:|" * len(language_names),
    ]
    descriptions = dict(ALGORITHMS)
    for algorithm, _ in ALGORITHMS:
        cells = [f"{medians[(algorithm, name)]:.3f}" for name in language_names]
        lines.append(f"| {descriptions[algorithm]} | " + " | ".join(cells) + " |")
    lines += [
        "",
        f"## Relative time versus {baseline_name}",
        "",
        "Lower is faster. The geometric mean gives each workload equal weight.",
        "",
        "| Language | " + " | ".join(algorithm for algorithm, _ in ALGORITHMS) + " | Geometric mean |",
        "|---|" + "---:|" * (len(ALGORITHMS) + 1),
    ]
    for language_name in language_names:
        cells = [f"{ratios[(algorithm, language_name)]:.2f}x" for algorithm, _ in ALGORITHMS]
        lines.append(f"| {language_name} | " + " | ".join(cells) + f" | {geomeans[language_name]:.2f}x |")
    lines += [
        "",
        "## Environment",
        "",
        "```json",
        json.dumps(metadata, indent=2, ensure_ascii=False),
        "```",
        "",
        "All runs validated deterministic checksums before their timings were accepted.",
    ]
    markdown = "\n".join(lines) + "\n"
    (ROOT / "results.md").write_text(markdown, encoding="utf-8")
    print(markdown)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
