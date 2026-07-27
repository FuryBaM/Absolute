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
    samples: int = 5


def languages() -> list[Language]:
    result: list[Language] = []
    absolute_dir = Path(os.environ.get("ABSOLUTE_BENCH_BIN_DIR", ROOT / "bin"))
    if all((absolute_dir / f"absolute-{name}").exists() for name, _ in ALGORITHMS):
        result.append(Language("Absolute", ("{absolute}",)))
    if (ROOT / "benchmark-cpp").exists():
        result.append(Language("C++", (str(ROOT / "benchmark-cpp"), "{algorithm}", "{loops}")))
    if (ROOT / "benchmark-rust").exists():
        result.append(Language("Rust", (str(ROOT / "benchmark-rust"), "{algorithm}", "{loops}")))
    if (ROOT / "benchmark-go").exists():
        result.append(Language("Go", (str(ROOT / "benchmark-go"), "{algorithm}", "{loops}")))
    if (ROOT / "Benchmark.class").exists():
        result.append(Language("Java", (
            "java", "-Xms512m", "-Xmx512m", "-XX:+UseSerialGC",
            "-cp", str(ROOT), "Benchmark", "{algorithm}", "{loops}",
        )))
    return result


def build_command(language: Language, algorithm: str) -> list[str]:
    loops = LOOPS[algorithm]
    absolute = Path(os.environ.get("ABSOLUTE_BENCH_BIN_DIR", ROOT / "bin")) / f"absolute-{algorithm}"
    return [part.format(algorithm=algorithm, loops=loops, absolute=absolute) for part in language.command]


def run_once(command: list[str], expected: int | None, timeout: int = 180) -> tuple[float, int]:
    completed = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=timeout)
    if completed.returncode != 0:
        raise RuntimeError(
            f"command failed ({completed.returncode}): {' '.join(command)}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    lines = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
    if len(lines) < 2:
        raise RuntimeError(f"command did not produce elapsed nanoseconds and checksum: {' '.join(command)}")
    try:
        elapsed_ns = int(lines[-2])
        checksum = int(lines[-1])
    except ValueError as exc:
        raise RuntimeError(f"invalid benchmark output from {' '.join(command)}: {lines[-2:]!r}") from exc
    if elapsed_ns <= 0:
        raise RuntimeError(f"non-positive internal duration from {' '.join(command)}: {elapsed_ns}")
    if expected is not None and checksum != expected:
        raise RuntimeError(
            f"checksum mismatch for {' '.join(command)}: expected {expected}, got {checksum}"
        )
    return elapsed_ns / 1_000_000.0, checksum


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
    if not any(language.name == "C++" for language in selected):
        raise RuntimeError("C++ reference implementation is required for checksum validation")

    expected_checksums: dict[str, int] = {}
    for algorithm, _ in ALGORITHMS:
        _, checksum = run_once(
            [str(ROOT / "benchmark-cpp"), algorithm, str(LOOPS[algorithm])],
            None,
        )
        expected_checksums[algorithm] = checksum

    measurements: dict[tuple[str, str], list[float]] = {
        (algorithm, language.name): []
        for algorithm, _ in ALGORITHMS
        for language in selected
    }

    for algorithm_index, (algorithm, _description) in enumerate(ALGORITHMS):
        loops = LOOPS[algorithm]
        max_samples = max(language.samples for language in selected)
        for sample_index in range(max_samples):
            shift = (algorithm_index + sample_index) % len(selected)
            order = selected[shift:] + selected[:shift]
            for language in order:
                if sample_index >= language.samples:
                    continue
                elapsed_ms, _ = run_once(
                    build_command(language, algorithm),
                    expected_checksums[algorithm],
                )
                per_iteration = elapsed_ms / loops
                measurements[(algorithm, language.name)].append(per_iteration)
                print(
                    f"{algorithm:10s} {language.name:10s} "
                    f"sample {sample_index + 1}/{language.samples}: "
                    f"{per_iteration:.6f} ms/iteration (internal timer)",
                    flush=True,
                )

    language_names = [language.name for language in selected]
    medians: dict[tuple[str, str], float] = {}
    rows: list[dict[str, object]] = []
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
                "min_ms": min(samples),
                "max_ms": max(samples),
                "samples": len(samples),
                "loops_per_process": LOOPS[algorithm],
                "validated_checksum": expected_checksums[algorithm],
                "timing_scope": "internal monotonic timer around measured kernel loop",
            })

    baseline_name = "Absolute" if "Absolute" in language_names else "C++"
    ratios: dict[tuple[str, str], float] = {}
    geomeans: dict[str, float] = {}
    for algorithm, _ in ALGORITHMS:
        baseline = medians[(algorithm, baseline_name)]
        for language_name in language_names:
            ratios[(algorithm, language_name)] = medians[(algorithm, language_name)] / baseline
    for language_name in language_names:
        geomeans[language_name] = math.exp(statistics.mean(
            math.log(ratios[(algorithm, language_name)])
            for algorithm, _ in ALGORITHMS
        ))

    with (ROOT / "results.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0].keys()) + ["relative_to_baseline"])
        writer.writeheader()
        for row in rows:
            writer.writerow({
                **row,
                "relative_to_baseline": ratios[(str(row["algorithm"]), str(row["language"]))],
            })

    metadata = {
        "platform": platform.platform(),
        "machine": platform.machine(),
        "processor": platform.processor(),
        "runner_python": version([sys.executable, "--version"]),
        "cpp": version(["clang++", "--version"]),
        "rust": version(["rustc", "--version"]),
        "go": version(["go", "version"]),
        "java": version(["java", "-version"]),
        "baseline": baseline_name,
        "samples_per_language_algorithm": 5,
        "native_warmup_iterations": 2,
        "java_warmup_iterations": 10,
        "timing": {
            "Absolute": "std.time.monotonicNanoseconds",
            "C++": "std::chrono::steady_clock",
            "Rust": "std::time::Instant",
            "Go": "time.Now/time.Since monotonic component",
            "Java": "System.nanoTime",
        },
        "excluded_from_timing": [
            "process startup",
            "runtime startup",
            "JVM startup",
            "warmup iterations",
            "checksum printing",
            "process shutdown",
        ],
    }
    (ROOT / "metadata.json").write_text(
        json.dumps(metadata, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )

    lines = [
        "# Internally timed complex algorithm benchmark",
        "",
        "Median kernel time per iteration in milliseconds. Each language uses its own monotonic clock inside the process.",
        "Process/runtime startup, warmup, output and shutdown are outside the timed region.",
        f"Relative baseline: **{baseline_name}**.",
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
        lines.append(
            f"| {language_name} | " + " | ".join(cells) + f" | {geomeans[language_name]:.2f}x |"
        )
    lines += [
        "",
        "## Environment and methodology",
        "",
        "```json",
        json.dumps(metadata, indent=2, ensure_ascii=False),
        "```",
        "",
        "All accepted samples produced the same deterministic checksum as the C++ reference.",
    ]
    markdown = "\n".join(lines) + "\n"
    (ROOT / "results.md").write_text(markdown, encoding="utf-8")
    print(markdown)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
