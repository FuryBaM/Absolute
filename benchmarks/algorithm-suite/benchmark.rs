// The Rust implementation of the algorithm suite.
//
// Written against benchmark-clang.cpp line for line. The floating-point loop
// is written as separate multiply and add for the same reason the C++ build
// passes -ffp-contract=off: Absolute emits the two instructions, and a fused
// multiply-add would measure different arithmetic rather than a faster
// compiler.

use std::env;

fn mix() -> i32 {
    let mut checksum: i32 = 0;
    for round in 0..5i32 {
        let mut state: u32 = (123_456_789i32 + round) as u32;
        for _ in 0..20_000_000i32 {
            state = state.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
            state ^= ((state as i32) >> 16) as u32;
        }
        checksum ^= state as i32;
    }
    checksum
}

fn primes() -> i32 {
    let mut count: i32 = 1;
    let mut candidate: i32 = 3;
    while candidate <= 2_000_000 {
        let mut is_prime = true;
        let mut divisor: i32 = 3;
        while divisor * divisor <= candidate {
            if candidate % divisor == 0 {
                is_prime = false;
                break;
            }
            divisor += 2;
        }
        if is_prime {
            count += 1;
        }
        candidate += 2;
    }
    count
}

fn collatz() -> i64 {
    let mut total_steps: i64 = 0;
    for seed in 1..=2_000_000i32 {
        let mut value = seed as i64;
        while value > 1 {
            value = if value % 2 == 0 { value / 2 } else { value * 3 + 1 };
            total_steps += 1;
        }
    }
    total_steps
}

fn gcd() -> i64 {
    let mut checksum: i64 = 0;
    for i in 1..=5_000_000i32 {
        let mut left = i.wrapping_mul(17).wrapping_add(12_345);
        let mut right = i.wrapping_mul(31).wrapping_add(6_789);
        while right != 0 {
            let remainder = left % right;
            left = right;
            right = remainder;
        }
        checksum += left as i64;
    }
    checksum
}

fn floating_point() -> i32 {
    let mut value: f64 = 0.5;
    let mut sum: f64 = 0.0;
    for _ in 0..100_000_000i32 {
        value = value * 1.000000119 + 0.0000001;
        if value > 2.0 {
            value -= 1.5;
        }
        sum += value;
    }
    sum as i32
}

fn main() {
    let arguments: Vec<String> = env::args().collect();
    if arguments.len() != 2 {
        std::process::exit(2);
    }
    match arguments[1].as_str() {
        "mix" => println!("{}", mix()),
        "primes" => println!("{}", primes()),
        "collatz" => println!("{}", collatz()),
        "gcd" => println!("{}", gcd()),
        "floating" => println!("{}", floating_point()),
        _ => std::process::exit(2),
    }
}
