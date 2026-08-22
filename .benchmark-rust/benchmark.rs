use std::env;
use std::process;

fn matrix_multiply(bench_seed: i32) -> i64 {
    const N: usize = 144;
    const REPEATS: usize = 4;
    let mut a = vec![0_i32; N * N];
    let mut b = vec![0_i32; N * N];
    let mut c = vec![0_i64; N * N];
    for i in 0..N {
        for j in 0..N {
            a[i * N + j] = (((i as i32) * 17 + (j as i32) * 13 + 7 + bench_seed * 3) % 31) - 15;
            b[i * N + j] = (((i as i32) * 11 + (j as i32) * 19 + 3 + bench_seed * 5) % 29) - 14;
        }
    }
    let mut checksum = 0_i64;
    for repeat in 0..REPEATS {
        for i in 0..N {
            for j in 0..N {
                let mut sum = 0_i64;
                for k in 0..N {
                    sum += (a[i * N + k] as i64) * (b[k * N + j] as i64);
                }
                c[i * N + j] = sum;
                checksum += sum * (((i + j + repeat) % 7 + 1) as i64);
            }
        }
        let index = (repeat * 997 + 17) % (N * N);
        a[index] += (repeat + 1) as i32;
    }
    checksum + c[(N / 2) * N + N / 3]
}

fn dijkstra_dense(bench_seed: i32) -> i64 {
    const N: usize = 900;
    const REPEATS: usize = 5;
    const INF: i32 = 1_000_000_000;
    let mut distance = vec![0_i32; N];
    let mut used = vec![false; N];
    let mut checksum = 0_i64;
    for repeat in 0..REPEATS {
        distance.fill(INF);
        used.fill(false);
        distance[(repeat * 137 + 11 + bench_seed as usize) % N] = 0;
        for _step in 0..N {
            let mut best: isize = -1;
            let mut best_distance = INF;
            for vertex in 0..N {
                if !used[vertex] && distance[vertex] < best_distance {
                    best_distance = distance[vertex];
                    best = vertex as isize;
                }
            }
            if best < 0 {
                break;
            }
            let best = best as usize;
            used[best] = true;
            for next in 0..N {
                if used[next] || next == best {
                    continue;
                }
                let weight = (((best as i32) * 31
                    + (next as i32) * 17
                    + (((best ^ next) as i32) * 13)
                    + (repeat as i32) * 7
                    + bench_seed * 17)
                    % 97)
                    + 1;
                let candidate = best_distance + weight;
                if candidate < distance[next] {
                    distance[next] = candidate;
                }
            }
        }
        for i in 0..N {
            checksum += (distance[i] as i64) * ((i + 1 + repeat) as i64);
        }
    }
    checksum
}

fn sequence_alignment(bench_seed: i32) -> i64 {
    const N: usize = 1500;
    const REPEATS: usize = 3;
    let mut left = vec![0_i32; N];
    let mut right = vec![0_i32; N];
    let mut previous = vec![0_i32; N + 1];
    let mut current = vec![0_i32; N + 1];
    for i in 0..N {
        left[i] = ((i as i32) * 17 + (i / 7) as i32 + 3 + bench_seed) % 23;
        right[i] = ((i as i32) * 19 + (i / 11) as i32 + 5 + bench_seed * 2) % 23;
    }
    let mut checksum = 0_i64;
    for repeat in 0..REPEATS {
        for j in 0..=N {
            previous[j] = j as i32;
        }
        for i in 1..=N {
            current[0] = i as i32;
            for j in 1..=N {
                let mut substitution = previous[j - 1];
                if left[i - 1] != right[j - 1] {
                    substitution += 2;
                }
                let deletion = previous[j] + 1;
                let insertion = current[j - 1] + 1;
                current[j] = substitution.min(deletion).min(insertion);
            }
            std::mem::swap(&mut previous, &mut current);
        }
        checksum += (previous[N] as i64) * ((repeat + 1) as i64);
        checksum += previous[(repeat * 499 + 101) % (N + 1)] as i64;
        let index = (repeat * 431 + 29) % N;
        right[index] = (right[index] + 1) % 23;
    }
    checksum
}

fn nbody(bench_seed: i32) -> i64 {
    const BODIES: usize = 96;
    const STEPS: usize = 250;
    let mut x = vec![0.0_f64; BODIES];
    let mut y = vec![0.0_f64; BODIES];
    let mut z = vec![0.0_f64; BODIES];
    let mut vx = vec![0.0_f64; BODIES];
    let mut vy = vec![0.0_f64; BODIES];
    let mut vz = vec![0.0_f64; BODIES];
    let mut mass = vec![0.0_f64; BODIES];
    for i in 0..BODIES {
        x[i] = ((((i as i32) * 17 + bench_seed * 3) % 101) - 50) as f64 * 0.013;
        y[i] = ((((i as i32) * 29 + bench_seed * 5) % 103) - 51) as f64 * 0.011;
        z[i] = ((((i as i32) * 37 + bench_seed * 7) % 107) - 53) as f64 * 0.009;
        vx[i] = ((((i as i32) * 7) % 17) - 8) as f64 * 0.00001;
        vy[i] = ((((i as i32) * 11) % 19) - 9) as f64 * 0.00001;
        vz[i] = ((((i as i32) * 13) % 23) - 11) as f64 * 0.00001;
        mass[i] = 0.5 + (i % 13) as f64 * 0.07;
    }
    for _step in 0..STEPS {
        for i in 0..BODIES {
            for j in (i + 1)..BODIES {
                let dx = x[j] - x[i];
                let dy = y[j] - y[i];
                let dz = z[j] - z[i];
                let inverse = 0.0000025 / (dx * dx + dy * dy + dz * dz + 0.015);
                let fx = dx * inverse;
                let fy = dy * inverse;
                let fz = dz * inverse;
                vx[i] += fx * mass[j];
                vy[i] += fy * mass[j];
                vz[i] += fz * mass[j];
                vx[j] -= fx * mass[i];
                vy[j] -= fy * mass[i];
                vz[j] -= fz * mass[i];
            }
        }
        for i in 0..BODIES {
            x[i] += vx[i];
            y[i] += vy[i];
            z[i] += vz[i];
        }
    }
    let mut total = 0.0_f64;
    for i in 0..BODIES {
        total += (x[i] * x[i] + y[i] * y[i] + z[i] * z[i]) * (i + 1) as f64;
        total += (vx[i] * vx[i] + vy[i] * vy[i] + vz[i] * vz[i]) * 1000.0;
    }
    (total * 1_000_000.0) as i64
}

fn mandelbrot(bench_seed: i32) -> i64 {
    const WIDTH: usize = 420;
    const HEIGHT: usize = 280;
    let max_iterations = 80 + bench_seed % 3;
    let mut checksum = 0_i64;
    for py in 0..HEIGHT {
        let cy = -1.2 + 2.4 * py as f64 / (HEIGHT - 1) as f64;
        for px in 0..WIDTH {
            let cx = -2.1 + 3.2 * px as f64 / (WIDTH - 1) as f64;
            let mut zx = 0.0_f64;
            let mut zy = 0.0_f64;
            let mut iteration = 0_i32;
            while iteration < max_iterations && zx * zx + zy * zy <= 4.0 {
                let next_x = zx * zx - zy * zy + cx;
                zy = 2.0 * zx * zy + cy;
                zx = next_x;
                iteration += 1;
            }
            checksum += (iteration as i64) * (((px + 3 * py) % 17 + 1) as i64);
        }
    }
    checksum
}

fn modular_power(mut base: i64, mut exponent: i64, modulus: i64) -> i64 {
    let mut result = 1_i64;
    while exponent > 0 {
        if exponent & 1 != 0 {
            result = result * base % modulus;
        }
        base = base * base % modulus;
        exponent >>= 1;
    }
    result
}

fn ntt_transform(values: &mut [i64], inverse: bool) {
    const MODULUS: i64 = 65_537;
    const PRIMITIVE_ROOT: i64 = 3;
    let n = values.len();
    let mut j = 0_usize;
    for i in 1..n {
        let mut bit = n >> 1;
        while j & bit != 0 {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if i < j {
            values.swap(i, j);
        }
    }
    let mut length = 2_usize;
    while length <= n {
        let mut root = modular_power(PRIMITIVE_ROOT, (MODULUS - 1) / length as i64, MODULUS);
        if inverse {
            root = modular_power(root, MODULUS - 2, MODULUS);
        }
        let half = length >> 1;
        let mut start = 0_usize;
        while start < n {
            let mut factor = 1_i64;
            for offset in 0..half {
                let even = values[start + offset];
                let odd = values[start + offset + half] * factor % MODULUS;
                values[start + offset] = (even + odd) % MODULUS;
                values[start + offset + half] = (even - odd + MODULUS) % MODULUS;
                factor = factor * root % MODULUS;
            }
            start += length;
        }
        length <<= 1;
    }
    if inverse {
        let inverse_n = modular_power(n as i64, MODULUS - 2, MODULUS);
        for value in values.iter_mut() {
            *value = *value * inverse_n % MODULUS;
        }
    }
}

fn ntt(bench_seed: i32) -> i64 {
    const N: usize = 16_384;
    const REPEATS: usize = 20;
    let mut values = vec![0_i64; N];
    for i in 0..N {
        let ii = i as i64;
        values[i] = (ii * ii * 17 + ii * 31 + 7 + bench_seed as i64 * 13) % 65_537;
    }
    let mut checksum = 0_i64;
    for repeat in 0..REPEATS {
        ntt_transform(&mut values, false);
        checksum += values[(repeat * 811 + 19) % N];
        ntt_transform(&mut values, true);
        let index = (repeat * 977 + 23) % N;
        values[index] = (values[index] + (repeat + 1) as i64) % 65_537;
    }
    let mut i = 0_usize;
    while i < N {
        checksum += values[i] * (i + 1) as i64;
        i += 257;
    }
    checksum
}

fn hash_join(bench_seed: i32) -> i64 {
    const TABLE_SIZE: usize = 1 << 20;
    const MASK: usize = TABLE_SIZE - 1;
    const BUILD_COUNT: usize = 300_000;
    const PROBE_COUNT: usize = 1_500_000;
    const REPEATS: usize = 3;
    let mut table = vec![0_i32; TABLE_SIZE];
    let mut checksum = 0_i64;
    for repeat in 0..REPEATS {
        table.fill(-1);
        for i in 0..BUILD_COUNT {
            let key = ((i as i64 * 104_729
                + 12_345
                + (repeat * 17) as i64
                + (bench_seed * 29) as i64)
                % 10_000_019) as i32;
            let mut slot = ((key as i64 * 40_503 + (key / 97) as i64 * 7_919) & MASK as i64) as usize;
            while table[slot] != -1 {
                slot = (slot + 1) & MASK;
            }
            table[slot] = key;
        }
        for i in 0..PROBE_COUNT {
            let key = if i & 3 == 0 {
                let source = (i * 13) % BUILD_COUNT;
                ((source as i64 * 104_729
                    + 12_345
                    + (repeat * 17) as i64
                    + (bench_seed * 29) as i64)
                    % 10_000_019) as i32
            } else {
                ((i as i64 * 130_363
                    + 7_654_321
                    + (repeat * 101) as i64
                    + (bench_seed * 31) as i64)
                    % 10_000_019) as i32
            };
            let mut slot = ((key as i64 * 40_503 + (key / 97) as i64 * 7_919) & MASK as i64) as usize;
            while table[slot] != -1 && table[slot] != key {
                slot = (slot + 1) & MASK;
            }
            if table[slot] == key {
                checksum += key as i64 * ((i & 7) + 1) as i64;
            }
        }
    }
    checksum
}

fn ray_sphere(bench_seed: i32) -> i64 {
    const SPHERE_COUNT: usize = 64;
    const RAY_COUNT: usize = 60_000;
    const REPEATS: usize = 3;
    let mut sx = vec![0.0_f64; SPHERE_COUNT];
    let mut sy = vec![0.0_f64; SPHERE_COUNT];
    let mut sz = vec![0.0_f64; SPHERE_COUNT];
    let mut radius = vec![0.0_f64; SPHERE_COUNT];
    for i in 0..SPHERE_COUNT {
        sx[i] = ((((i as i32) * 37) % 97) - 48) as f64 * 0.08;
        sy[i] = ((((i as i32) * 53) % 89) - 44) as f64 * 0.07;
        sz[i] = 3.0 + (((i as i32) * 29) % 71) as f64 * 0.06;
        radius[i] = 0.25 + (i % 11) as f64 * 0.025;
    }
    let mut checksum = 0_i64;
    for repeat in 0..REPEATS {
        for ray in 0..RAY_COUNT {
            let dx = ((((ray as i32) * 17 + (repeat as i32) * 13 + bench_seed * 19) % 2001) - 1000) as f64 / 1000.0;
            let dy = ((((ray as i32) * 29 + (repeat as i32) * 7 + bench_seed * 23) % 2003) - 1001) as f64 / 1000.0;
            let dz = 1.5 + (((ray as i32) * 11) % 101) as f64 / 200.0;
            let a = dx * dx + dy * dy + dz * dz;
            for sphere in 0..SPHERE_COUNT {
                let ox = -sx[sphere];
                let oy = -sy[sphere];
                let oz = -sz[sphere];
                let half_b = ox * dx + oy * dy + oz * dz;
                let c = ox * ox + oy * oy + oz * oz - radius[sphere] * radius[sphere];
                let discriminant = half_b * half_b - a * c;
                if discriminant > 0.0 && half_b < 0.0 {
                    checksum += (sphere + 1 + repeat) as i64;
                }
            }
        }
    }
    checksum
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
    let mut result = 0_i64;
    for i in 0..loops {
        let bench_seed = i as i32 * 97 + 11;
        result += match args[1].as_str() {
            "matrix" => matrix_multiply(bench_seed),
            "dijkstra" => dijkstra_dense(bench_seed),
            "alignment" => sequence_alignment(bench_seed),
            "nbody" => nbody(bench_seed),
            "mandelbrot" => mandelbrot(bench_seed),
            "ntt" => ntt(bench_seed),
            "hashjoin" => hash_join(bench_seed),
            "raytrace" => ray_sphere(bench_seed),
            _ => process::exit(2),
        };
    }
    println!("{result}");
}
