// The Rust implementation of the array suite.
//
// Written against benchmark.cpp line for line. The arrays are locals, as they
// are in the C++ and in the Absolute -- the suite reserves a large stack for
// exactly that -- so this measures the same storage the other languages use
// rather than a heap allocation that happens to hold the same numbers.

use std::env;

fn scan() -> i64 {
    const SIZE: usize = 1_000_000;
    let mut values = [0u32; SIZE];
    for index in 0..SIZE {
        values[index] = (index as u32).wrapping_mul(17).wrapping_add(3);
    }
    let mut checksum: i64 = 0;
    for round in 0..100i32 {
        for index in 0..SIZE {
            values[index] = values[index].wrapping_mul(3).wrapping_add(round as u32);
            checksum += values[index] as i32 as i64;
        }
    }
    checksum
}

fn random_access() -> i64 {
    const SIZE: usize = 1_048_576;
    const MASK: u32 = SIZE as u32 - 1;
    let mut values = [0i32; SIZE];
    for index in 0..SIZE {
        values[index] = (index as i32).wrapping_mul(31).wrapping_add(7);
    }
    let mut state: u32 = 123_456_789;
    let mut checksum: i64 = 0;
    for _ in 0..100_000_000i32 {
        state = state.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
        let index = (state & MASK) as usize;
        values[index] = ((values[index] as u32) ^ state) as i32;
        checksum += values[index] as i64;
    }
    checksum
}

fn insertion_sort() -> i64 {
    const SIZE: usize = 30_000;
    let mut values = [0i32; SIZE];
    let mut state: u32 = 123_456_789;
    for index in 0..SIZE {
        state = state.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
        values[index] = state as i32;
    }
    for index in 1..SIZE {
        let key = values[index];
        let mut position = index as isize - 1;
        while position >= 0 {
            if values[position as usize] <= key {
                break;
            }
            values[position as usize + 1] = values[position as usize];
            position -= 1;
        }
        values[(position + 1) as usize] = key;
    }
    let mut checksum: i64 = 0;
    for index in 0..SIZE {
        checksum += values[index] as i64;
    }
    checksum += values[0] as i64 * 31;
    checksum += values[SIZE - 1] as i64 * 17;
    checksum
}

fn main() {
    let arguments: Vec<String> = env::args().collect();
    if arguments.len() != 2 {
        std::process::exit(2);
    }
    match arguments[1].as_str() {
        "scan" => println!("{}", scan()),
        "random-access" => println!("{}", random_access()),
        "insertion-sort" => println!("{}", insertion_sort()),
        _ => std::process::exit(2),
    }
}
