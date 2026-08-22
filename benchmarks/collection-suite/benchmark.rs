// The Rust implementation of the collection suite.
//
// Written against benchmark.cpp line for line rather than idiomatically: the
// suite compares one algorithm across languages, so a port that reaches the
// same answer by a different route measures a different thing. Where the C++
// relies on unsigned wraparound the wrapping operations are written out, so
// the arithmetic does not depend on which profile the compiler was given.
//
// `HashMap` here is the standard one, with the hasher a Rust program gets by
// default. It is DoS-resistant and therefore slower than the one C++ hands
// out; using a faster third-party hasher would measure a different library
// than the one the language ships. The README says so beside the numbers.

use std::collections::HashMap;
use std::env;

// Push 1M elements, sum all, pop 500K, return sum + remaining count.
fn vector_push_sum() -> i64 {
    const N: i32 = 1_000_000;
    let mut v: Vec<i32> = Vec::with_capacity(N as usize);
    for i in 0..N {
        v.push(i.wrapping_mul(17).wrapping_add(3));
    }
    let mut checksum: i64 = 0;
    for i in 0..N as usize {
        checksum += v[i] as i64;
    }
    for _ in 0..500_000 {
        v.pop();
    }
    checksum += v.len() as i64;
    checksum
}

// Push 100K pseudorandom i32, insertion-sort, checksum.
fn vector_sort() -> i64 {
    const SIZE: i32 = 100_000;
    let mut values: Vec<i32> = Vec::with_capacity(SIZE as usize);
    let mut state: u32 = 123_456_789;
    for _ in 0..SIZE {
        state = state.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
        values.push(state as i32);
    }
    for i in 1..SIZE as usize {
        let key = values[i];
        let mut j = i as isize - 1;
        while j >= 0 && values[j as usize] > key {
            values[j as usize + 1] = values[j as usize];
            j -= 1;
        }
        values[(j + 1) as usize] = key;
    }
    let mut checksum: i64 = 0;
    for i in 0..SIZE as usize {
        checksum += values[i] as i64;
    }
    checksum += values[0] as i64 * 31;
    checksum += values[SIZE as usize - 1] as i64 * 17;
    checksum
}

// Insert 200K entries, look every one up, sum the values.
fn hashmap_insert_lookup() -> i64 {
    const N: i32 = 200_000;
    let mut map: HashMap<i32, i32> = HashMap::with_capacity((N * 2) as usize);
    for i in 0..N {
        let k = i.wrapping_mul(17).wrapping_add(31);
        let v = i.wrapping_mul(13).wrapping_add(7);
        map.insert(k, v);
    }
    let mut checksum: i64 = 0;
    for i in 0..N {
        let k = i.wrapping_mul(17).wrapping_add(31);
        if let Some(v) = map.get(&k) {
            checksum += *v as i64;
        }
    }
    checksum
}

fn main() {
    let arguments: Vec<String> = env::args().collect();
    if arguments.len() != 2 {
        std::process::exit(2);
    }
    match arguments[1].as_str() {
        "vector-push-sum" => println!("{}", vector_push_sum()),
        "vector-sort" => println!("{}", vector_sort()),
        "hashmap-insert-lookup" => println!("{}", hashmap_insert_lookup()),
        _ => std::process::exit(2),
    }
}
