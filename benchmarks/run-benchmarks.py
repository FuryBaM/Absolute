#!/usr/bin/env python3
"""Run the Absolute multi-language benchmark suites on Linux and macOS.

The suites originally shipped with Windows-only drivers (`run.bat` plus
`run-benchmark.ps1`) that need MSVC and, for the Absolute side, a WSL hop.
This script reproduces the same methodology on a POSIX host:

* Absolute is compiled with `absolutec --emit-llvm` and the emitted IR is
  optimized by `clang -O3 -march=native`, matching the WSL backend path.
* C++ uses the same `-O3 -march=native` settings.
* Every run is validated against the suite checksum before its time counts.
* Optimized/JIT languages are measured with rotating order and reported as a
  median; Python is sampled separately because it is far slower.
* Timing is end-to-end process wall time, including runtime startup and JIT.

Toolchains that are not installed are skipped with a note instead of aborting
the run, so a host without the .NET SDK still measures every other language.
"""

from __future__ import annotations

import argparse
import csv
import os
import resource
import shutil
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

SUITE_ROOT = Path(__file__).resolve().parent
REPO_ROOT = SUITE_ROOT.parent

# Absolute allocates local arrays on the stack, so the native benchmarks need
# the same headroom the Windows driver reserves with `/stack:67108864`.
NATIVE_STACK_BYTES = 512 * 1024 * 1024


@dataclass(frozen=True)
class Suite:
    name: str
    algorithms: tuple[str, ...]
    # None means the suite has no published checksum and instead requires every
    # implementation and sample to agree with the first run it produces.
    checksums: dict[str, str] | None
    cpp_source: str = "benchmark.cpp"
    cpp_extra_flags: tuple[str, ...] = ()
    # Extra C++ sources linked into the Absolute executables (runtime support).
    absolute_runtime_sources: dict[str, tuple[str, ...]] = field(default_factory=dict)
    # Link the built Absolute runtime library into the Absolute executables.
    needs_absolute_runtime: bool = False
    # Languages this suite provides an implementation for.
    languages: tuple[str, ...] = ("Absolute", "C++", "C#", "Java", "JavaScript", "Python")


SUITES: dict[str, Suite] = {
    "array": Suite(
        name="array-suite",
        algorithms=("scan", "random-access", "insertion-sort"),
        checksums={
            "scan": "1063467781802240",
            "random-access": "268750506018432",
            "insertion-sort": "312098439810",
        },
    ),
    "pointer-object": Suite(
        name="pointer-object-suite",
        algorithms=("managed-deref", "heap-nodes", "arena-graph", "object-tree"),
        checksums={
            "managed-deref": "2007139840",
            "heap-nodes": "765920640",
            "arena-graph": "1147923655",
            "object-tree": "1238767148",
        },
        absolute_runtime_sources={
            "managed-deref": ("Absolute-Runtime/src/pointers.cpp",),
        },
    ),
    "algorithm": Suite(
        name="algorithm-suite",
        algorithms=("mix", "primes", "collatz", "gcd", "floating"),
        checksums={
            "mix": "634231978",
            "primes": "148933",
            "collatz": "277182223",
            "gcd": "68856518",
            "floating": "115032218",
        },
        cpp_source="benchmark-clang.cpp",
        # Absolute emits separate multiply and add instructions; disable
        # contraction so C++ measures the same arithmetic.
        cpp_extra_flags=("-ffp-contract=off",),
    ),
    "collection": Suite(
        name="collection-suite",
        algorithms=("vector-push-sum", "vector-sort", "hashmap-insert-lookup"),
        checksums={
            "vector-push-sum": "8499995000000",
            "vector-sort": "43156608257",
            "hashmap-insert-lookup": "260000100000",
        },
        needs_absolute_runtime=True,
    ),
    "value-ref": Suite(
        name="value-ref-suite",
        # Three parameter-passing modes of the same workload, compared against
        # each other rather than against another language.
        algorithms=("by-value", "const-ref", "raw-borrow"),
        checksums=None,
        needs_absolute_runtime=True,
        languages=("Absolute",),
    ),
}

OPTIMIZED_LANGUAGES = ("Absolute", "C++", "C#", "Java", "JavaScript")


def raise_stack_limit() -> None:
    """Give the current process (and its children) a large stack."""
    soft, hard = resource.getrlimit(resource.RLIMIT_STACK)
    target = NATIVE_STACK_BYTES if hard == resource.RLIM_INFINITY else min(NATIVE_STACK_BYTES, hard)
    if soft == resource.RLIM_INFINITY or soft >= target:
        return
    try:
        resource.setrlimit(resource.RLIMIT_STACK, (target, hard))
    except (ValueError, OSError) as error:
        print(f"warning: could not raise the stack limit: {error}", file=sys.stderr)


def find_tool(*candidates: str) -> str | None:
    for candidate in candidates:
        found = shutil.which(candidate)
        if found:
            return found
    return None


def run(label: str, command: list[str], **kwargs) -> None:
    result = subprocess.run(command, **kwargs)
    if result.returncode != 0:
        raise SystemExit(f"{label} failed with exit code {result.returncode}")


def build_compiler(build_dir: Path, jobs: int) -> Path:
    """Configure and build a Release Absolute compiler if it is missing."""
    compiler = build_dir / "Release" / "absolutec"
    if compiler.exists():
        print(f"[Reusing Absolute compiler] {compiler}")
        return compiler

    cmake = find_tool("cmake")
    if not cmake:
        raise SystemExit("cmake was not found in PATH")
    llvm_config = find_tool("llvm-config-18", "llvm-config")
    if not llvm_config:
        raise SystemExit("llvm-config was not found in PATH")
    llvm_dir = subprocess.run(
        [llvm_config, "--cmakedir"], capture_output=True, text=True, check=True
    ).stdout.strip()

    print("[Configure Absolute Release compiler]")
    run("cmake configure", [
        cmake, "-S", str(REPO_ROOT), "-B", str(build_dir),
        "-DCMAKE_BUILD_TYPE=Release", "-DABSOLUTE_ENABLE_LLVM=ON",
        f"-DLLVM_DIR={llvm_dir}",
    ])
    print("[Build Absolute Release compiler]")
    run("cmake build", [cmake, "--build", str(build_dir), "--parallel", str(jobs)])

    if not compiler.exists():
        raise SystemExit(f"Absolute compiler was not produced at {compiler}")
    return compiler


@dataclass
class Runner:
    """How to launch one language implementation of one algorithm."""
    command: list[str]
    per_algorithm: bool = True


def find_absolute_runtime(compiler: Path) -> Path | None:
    """Locate libAbsolute-Runtime.a in the build tree that produced the compiler."""
    for directory in (compiler.parent, compiler.parent.parent):
        candidate = directory / "libAbsolute-Runtime.a"
        if candidate.exists():
            return candidate
    return None


def compile_suite(
    suite: Suite,
    compiler: Path,
    native_out: Path,
    languages: set[str],
) -> dict[str, dict[str, Runner]]:
    """Compile every available implementation; return runners keyed by language."""
    suite_dir = SUITE_ROOT / suite.name
    native_out.mkdir(parents=True, exist_ok=True)
    runners: dict[str, dict[str, Runner]] = {}

    clang = find_tool("clang++-18", "clang++")
    if not clang:
        raise SystemExit("clang++ was not found in PATH")

    if "Absolute" in languages:
        print("[Compile Absolute benchmarks]")
        absolute: dict[str, Runner] = {}
        for algorithm in suite.algorithms:
            source = suite_dir / "absolute" / f"{algorithm}.abs"
            if not source.exists():
                print(f"  skip {algorithm}: {source} is missing")
                continue
            ir = native_out / f"absolute-{algorithm}.ll"
            executable = native_out / f"absolute-{algorithm}"
            # absolutec resolves `std/...` imports relative to its working
            # directory, so emit from the repository root.
            run(f"absolutec {algorithm}",
                [str(compiler), str(source), "--emit-llvm", "-o", str(ir)], cwd=REPO_ROOT)
            extra = [
                str(REPO_ROOT / relative)
                for relative in suite.absolute_runtime_sources.get(algorithm, ())
            ]
            if suite.needs_absolute_runtime:
                runtime_library = find_absolute_runtime(compiler)
                if not runtime_library:
                    raise SystemExit(
                        "The Absolute runtime library was not found next to the compiler; "
                        "build the project before running this suite."
                    )
                extra.append(str(runtime_library))
            run(f"clang {algorithm}", [
                clang, "-O3", "-march=native", "-DNDEBUG",
                str(ir), *extra, "-o", str(executable),
            ])
            absolute[algorithm] = Runner([str(executable)])
        if absolute:
            runners["Absolute"] = absolute

    if "C++" in languages:
        print("[Compile C++ benchmark]")
        cpp_source = suite_dir / suite.cpp_source
        cpp_executable = native_out / "benchmark-cpp"
        run("clang++ benchmark", [
            clang, "-std=c++20", "-O3", "-march=native", "-DNDEBUG",
            "-fno-exceptions", "-fno-rtti", *suite.cpp_extra_flags,
            str(cpp_source), "-o", str(cpp_executable),
        ])
        runners["C++"] = {
            algorithm: Runner([str(cpp_executable), algorithm])
            for algorithm in suite.algorithms
        }

    if "C#" in languages:
        dotnet = find_tool("dotnet")
        if not dotnet:
            print("[Skip C#] the .NET SDK was not found in PATH")
        else:
            print("[Compile C# Release benchmark]")
            dotnet_out = native_out.parent / "dotnet"
            run("dotnet publish", [
                dotnet, "publish", str(suite_dir / "Benchmark.csproj"),
                "-c", "Release", "-o", str(dotnet_out), "--nologo",
                f"-p:BaseOutputPath={native_out.parent / 'dotnet-bin'}/",
                f"-p:BaseIntermediateOutputPath={native_out.parent / 'dotnet-obj'}/",
            ])
            runners["C#"] = {
                algorithm: Runner([str(dotnet_out / "Benchmark"), algorithm])
                for algorithm in suite.algorithms
            }

    if "Java" in languages:
        javac, java = find_tool("javac"), find_tool("java")
        if not javac or not java:
            print("[Skip Java] javac or java was not found in PATH")
        else:
            print("[Compile Java benchmark]")
            java_out = native_out.parent / "java"
            java_out.mkdir(parents=True, exist_ok=True)
            run("javac", [javac, "-d", str(java_out), str(suite_dir / "Benchmark.java")])
            runners["Java"] = {
                algorithm: Runner([java, "-cp", str(java_out), "Benchmark", algorithm])
                for algorithm in suite.algorithms
            }

    if "JavaScript" in languages:
        node = find_tool("node")
        if not node:
            print("[Skip JavaScript] node was not found in PATH")
        else:
            runners["JavaScript"] = {
                algorithm: Runner([node, str(suite_dir / "benchmark.js"), algorithm])
                for algorithm in suite.algorithms
            }

    if "Python" in languages:
        python = find_tool("python3", "python")
        if not python:
            print("[Skip Python] python3 was not found in PATH")
        else:
            runners["Python"] = {
                algorithm: Runner([python, str(suite_dir / "benchmark.py"), algorithm])
                for algorithm in suite.algorithms
            }

    return runners


def measure(runner: Runner, expected: str | None, label: str,
            observed: dict[str, str] | None = None, key: str = "") -> float:
    """Run one implementation once and return its wall time in seconds.

    `expected` of None means the suite publishes no checksum; the first result
    recorded in `observed` under `key` becomes the value every later run must
    reproduce.
    """
    start = time.perf_counter()
    result = subprocess.run(runner.command, capture_output=True, text=True)
    elapsed = time.perf_counter() - start

    if result.returncode != 0:
        raise SystemExit(
            f"{label} failed with exit code {result.returncode}\n{result.stderr.strip()}"
        )
    lines = [line for line in result.stdout.strip().splitlines() if line.strip()]
    actual = lines[-1].strip() if lines else ""
    if expected is None and observed is not None:
        expected = observed.setdefault(key, actual)
    if expected is not None and actual != expected:
        raise SystemExit(f"Checksum mismatch for {label}: expected {expected}, got {actual}")
    return elapsed


def run_suite(suite: Suite, args: argparse.Namespace, compiler: Path) -> list[dict[str, str]]:
    suite_dir = SUITE_ROOT / suite.name
    build_root = suite_dir / ".benchmark-build"
    native_out = build_root / "native"

    # A suite only ships some languages; never look for the others.
    languages = set(args.languages) & set(suite.languages)
    runners = compile_suite(suite, compiler, native_out, languages)

    # Checksums observed at runtime for suites that publish none.
    observed: dict[str, str] = {}

    def expected_for(algorithm: str) -> str | None:
        return suite.checksums[algorithm] if suite.checksums else None

    optimized = [language for language in OPTIMIZED_LANGUAGES if language in runners]
    if not optimized:
        raise SystemExit(f"No usable implementations were compiled for {suite.name}")

    measurements: dict[tuple[str, str], list[float]] = {}
    for algorithm in suite.algorithms:
        expected = expected_for(algorithm)
        available = [language for language in optimized if algorithm in runners[language]]
        for language in available:
            measurements[(algorithm, language)] = []

        if args.warmups > 0:
            print(f"[Warm up: {algorithm}]")
            for language in available:
                for _ in range(args.warmups):
                    measure(runners[language][algorithm], expected,
                        f"{language}/{algorithm}", observed, suite.name)

        print(f"[Measure: {algorithm}, {args.samples} samples per optimized language]")
        for round_index in range(args.samples):
            # Rotate the order every round so cache and thermal state do not
            # systematically favor whichever language happens to go first.
            for offset in range(len(available)):
                language = available[(round_index + offset) % len(available)]
                measurements[(algorithm, language)].append(
                    measure(runners[language][algorithm], expected,
                        f"{language}/{algorithm}", observed, suite.name)
                )

    if "Python" in runners:
        print(f"[Measure Python: {args.python_samples} sample(s) per algorithm]")
        for algorithm in suite.algorithms:
            expected = expected_for(algorithm)
            measurements[(algorithm, "Python")] = [
                measure(runners["Python"][algorithm], expected,
                    f"Python/{algorithm}", observed, suite.name)
                for _ in range(args.python_samples)
            ]

    rows: list[dict[str, str]] = []
    for algorithm in suite.algorithms:
        for language in list(OPTIMIZED_LANGUAGES) + ["Python"]:
            values = measurements.get((algorithm, language))
            if not values:
                continue
            rows.append({
                "Algorithm": algorithm,
                "Language": language,
                "Samples": str(len(values)),
                "MedianSeconds": f"{statistics.median(values):.6f}",
                "MinSeconds": f"{min(values):.6f}",
                "MaxSeconds": f"{max(values):.6f}",
                "Checksum": (suite.checksums[algorithm] if suite.checksums
                    else observed.get(suite.name, "")),
            })
    return rows


def report(suite: Suite, rows: list[dict[str, str]]) -> None:
    languages = list(dict.fromkeys(row["Language"] for row in rows))
    medians = {
        (row["Algorithm"], row["Language"]): float(row["MedianSeconds"]) for row in rows
    }

    width = max(len(algorithm) for algorithm in suite.algorithms) + 2
    print(f"\n== {suite.name}: median seconds ==")
    print("Algorithm".ljust(width) + "".join(language.rjust(14) for language in languages))
    for algorithm in suite.algorithms:
        cells = "".join(
            (f"{medians[(algorithm, language)]:.6f}" if (algorithm, language) in medians else "-")
            .rjust(14)
            for language in languages
        )
        print(algorithm.ljust(width) + cells)

    # A single-language suite compares its own variants, so a table of every
    # row against itself would only ever print 1.00x.
    if "Absolute" not in languages or len(languages) < 2:
        return
    print(f"\n== {suite.name}: time relative to Absolute (lower is faster) ==")
    print("Algorithm".ljust(width) + "".join(language.rjust(14) for language in languages))
    ratios: dict[str, list[float]] = {language: [] for language in languages}
    for algorithm in suite.algorithms:
        baseline = medians.get((algorithm, "Absolute"))
        if not baseline:
            continue
        cells = ""
        for language in languages:
            value = medians.get((algorithm, language))
            if value is None:
                cells += "-".rjust(14)
                continue
            ratio = value / baseline
            ratios[language].append(ratio)
            cells += f"{ratio:.2f}x".rjust(14)
        print(algorithm.ljust(width) + cells)

    geomean = "".join(
        (f"{statistics.geometric_mean(ratios[language]):.2f}x" if ratios[language] else "-").rjust(14)
        for language in languages
    )
    print("geomean".ljust(width) + geomean)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--suite", action="append", choices=[*SUITES, "all"], default=None,
        help="benchmark suite to run (repeatable, default: all)",
    )
    parser.add_argument("--samples", type=int, default=15, help="measured runs per optimized language")
    parser.add_argument("--warmups", type=int, default=2, help="unmeasured runs before measuring")
    parser.add_argument("--python-samples", type=int, default=1, help="measured runs for Python")
    parser.add_argument("--include-python", action="store_true", help="also measure Python")
    parser.add_argument(
        "--languages", default=",".join([*OPTIMIZED_LANGUAGES]),
        help="comma-separated languages to measure",
    )
    parser.add_argument(
        "--compiler", type=Path, default=None,
        help="path to an existing absolutec binary (skips the compiler build)",
    )
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4, help="compiler build parallelism")
    args = parser.parse_args()

    args.languages = [language.strip() for language in args.languages.split(",") if language.strip()]
    if args.include_python and "Python" not in args.languages:
        args.languages.append("Python")

    selected = args.suite or ["all"]
    names = list(SUITES) if "all" in selected else list(dict.fromkeys(selected))

    raise_stack_limit()

    if args.compiler:
        compiler = args.compiler.resolve()
        if not compiler.exists():
            raise SystemExit(f"Absolute compiler was not found at {compiler}")
    else:
        compiler = build_compiler(REPO_ROOT / "build", args.jobs)

    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    for name in names:
        suite = SUITES[name]
        print(f"\n=== {suite.name} ===")
        rows = run_suite(suite, args, compiler)
        report(suite, rows)

        results = SUITE_ROOT / suite.name / "results"
        results.mkdir(parents=True, exist_ok=True)
        output = results / f"{suite.name}-linux-{stamp}.csv"
        with output.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
        print(f"\nWrote {output}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
