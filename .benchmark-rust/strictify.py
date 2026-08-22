#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path.cwd()

# Absolute: compile each algorithm as a standalone executable and use std.time's
# monotonic nanosecond clock around only the measured kernel loop.
for path in sorted((ROOT / "absolute").glob("*.abs")):
    text = path.read_text()
    match = re.search(r"int32 main\(\) \{\s*int32 loops = (\d+);", text)
    if not match:
        raise RuntimeError(f"Absolute benchmark main not found in {path}")
    loops = int(match.group(1))
    main_index = text.index("int32 main() {")
    prefix = text[:main_index]
    if not prefix.startswith('import "../../../std/time.abs";'):
        prefix = 'import "../../../std/time.abs";\n\n' + prefix
    timed_main = f'''int32 main() {{
    int32 loops = {loops};
    int32 warmups = 2;
    int32 i = 0;
    int64 warmupChecksum = 0;
    while (i < warmups) {{
        warmupChecksum += kernel((loops + i) * 97 + 11);
        i += 1;
    }}
    int64 startedAt = std.time.monotonicNanoseconds();
    i = 0;
    int64 checksum = 0;
    while (i < loops) {{
        checksum += kernel(i * 97 + 11);
        i += 1;
    }}
    int64 elapsedNanoseconds = std.time.monotonicNanoseconds() - startedAt;
    if (warmupChecksum == -9223372036854775807) {{
        println(warmupChecksum);
    }}
    println(elapsedNanoseconds);
    println(checksum);
    return 0;
}}
'''
    path.write_text(prefix + timed_main)

cpp = ROOT / "benchmark.cpp"
text = cpp.read_text()
if "#include <chrono>" not in text:
    text = text.replace("#include <algorithm>\n", "#include <algorithm>\n#include <chrono>\n", 1)
main_index = text.index("int main(int argc, char** argv) {")
cpp_main = r'''int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const std::string name = argv[1];
    const int loops = std::atoi(argv[2]);
    if (loops <= 0) return 2;
    i64 (*kernel)() = nullptr;
    if (name == "matrix") kernel = matrix_multiply;
    else if (name == "dijkstra") kernel = dijkstra_dense;
    else if (name == "alignment") kernel = sequence_alignment;
    else if (name == "nbody") kernel = nbody;
    else if (name == "mandelbrot") kernel = mandelbrot;
    else if (name == "ntt") kernel = ntt;
    else if (name == "hashjoin") kernel = hash_join;
    else if (name == "raytrace") kernel = ray_sphere;
    else return 2;

    volatile i64 warmup_checksum = 0;
    for (int warmup = 0; warmup < 2; ++warmup) {
        bench_seed = (loops + warmup) * 97 + 11;
        warmup_checksum += kernel();
    }
    const auto started_at = std::chrono::steady_clock::now();
    i64 result = 0;
    for (int loop = 0; loop < loops; ++loop) {
        bench_seed = loop * 97 + 11;
        result += kernel();
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started_at).count();
    if (warmup_checksum == -9223372036854775807LL) {
        std::printf("%lld\n", static_cast<long long>(warmup_checksum));
    }
    std::printf("%lld\n%lld\n", static_cast<long long>(elapsed), static_cast<long long>(result));
    return 0;
}
'''
cpp.write_text(text[:main_index] + cpp_main)

go = ROOT / "benchmark.go"
text = go.read_text()
if '\t"time"\n' not in text:
    text = text.replace('\t"strconv"\n', '\t"strconv"\n\t"time"\n', 1)
main_index = text.index("func main() {")
go_main = r'''func main() {
	if len(os.Args) != 3 {
		os.Exit(2)
	}
	loops, err := strconv.Atoi(os.Args[2])
	if err != nil || loops <= 0 {
		os.Exit(2)
	}
	var kernel func() int64
	switch os.Args[1] {
	case "matrix":
		kernel = matrixMultiply
	case "dijkstra":
		kernel = dijkstraDense
	case "alignment":
		kernel = sequenceAlignment
	case "nbody":
		kernel = nbody
	case "mandelbrot":
		kernel = mandelbrot
	case "ntt":
		kernel = ntt
	case "hashjoin":
		kernel = hashJoin
	case "raytrace":
		kernel = raySphere
	default:
		os.Exit(2)
	}
	var warmupChecksum int64
	for i := 0; i < 2; i++ {
		benchSeed = (loops+i)*97 + 11
		warmupChecksum += kernel()
	}
	startedAt := time.Now()
	var result int64
	for i := 0; i < loops; i++ {
		benchSeed = i*97 + 11
		result += kernel()
	}
	elapsedNanoseconds := time.Since(startedAt).Nanoseconds()
	if warmupChecksum == -9223372036854775807 {
		fmt.Println(warmupChecksum)
	}
	fmt.Println(elapsedNanoseconds)
	fmt.Println(result)
}
'''
go.write_text(text[:main_index] + go_main)

java = ROOT / "Benchmark.java"
text = java.read_text()
main_index = text.index("    public static void main(String[] args) {")
java_tail = r'''    static long runKernel(String name) {
        return switch (name) {
            case "matrix" -> matrixMultiply();
            case "dijkstra" -> dijkstraDense();
            case "alignment" -> sequenceAlignment();
            case "nbody" -> nbody();
            case "mandelbrot" -> mandelbrot();
            case "ntt" -> ntt();
            case "hashjoin" -> hashJoin();
            case "raytrace" -> raySphere();
            default -> throw new IllegalArgumentException(name);
        };
    }

    public static void main(String[] args) {
        if (args.length != 2) System.exit(2);
        int loops = Integer.parseInt(args[1]);
        if (loops <= 0) System.exit(2);
        long warmupChecksum = 0;
        for (int warmup = 0; warmup < 10; ++warmup) {
            benchSeed = (loops + warmup) * 97 + 11;
            warmupChecksum += runKernel(args[0]);
        }
        long startedAt = System.nanoTime();
        long result = 0;
        for (int loop = 0; loop < loops; ++loop) {
            benchSeed = loop * 97 + 11;
            result += runKernel(args[0]);
        }
        long elapsedNanoseconds = System.nanoTime() - startedAt;
        if (warmupChecksum == Long.MIN_VALUE + 1) System.out.println(warmupChecksum);
        System.out.println(elapsedNanoseconds);
        System.out.println(result);
    }
}
'''
java.write_text(text[:main_index] + java_tail)

rust = ROOT / "benchmark.rs"
if rust.exists():
    text = rust.read_text()
    if "use std::time::Instant;" not in text:
        text = text.replace("use std::process;\n", "use std::process;\nuse std::time::Instant;\n", 1)
    main_index = text.index("fn main() {")
    rust_main = r'''fn run_kernel(name: &str, bench_seed: i32) -> i64 {
    match name {
        "matrix" => matrix_multiply(bench_seed),
        "dijkstra" => dijkstra_dense(bench_seed),
        "alignment" => sequence_alignment(bench_seed),
        "nbody" => nbody(bench_seed),
        "mandelbrot" => mandelbrot(bench_seed),
        "ntt" => ntt(bench_seed),
        "hashjoin" => hash_join(bench_seed),
        "raytrace" => ray_sphere(bench_seed),
        _ => process::exit(2),
    }
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() != 3 {
        process::exit(2);
    }
    let loops: usize = match args[2].parse() {
        Ok(value) if value > 0 => value,
        _ => process::exit(2),
    };
    let mut warmup_checksum = 0_i64;
    for warmup in 0..2 {
        warmup_checksum = warmup_checksum.wrapping_add(run_kernel(
            args[1].as_str(),
            ((loops + warmup) as i32) * 97 + 11,
        ));
    }
    std::hint::black_box(warmup_checksum);
    let started_at = Instant::now();
    let mut result = 0_i64;
    for i in 0..loops {
        result = result.wrapping_add(run_kernel(args[1].as_str(), i as i32 * 97 + 11));
    }
    let elapsed_nanoseconds = started_at.elapsed().as_nanos();
    println!("{elapsed_nanoseconds}");
    println!("{result}");
}
'''
    rust.write_text(text[:main_index] + rust_main)
