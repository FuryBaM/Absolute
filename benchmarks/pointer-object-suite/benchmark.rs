// The Rust implementation of the pointer-object suite.
//
// Written against benchmark.cpp line for line, in safe Rust: `Box` where the
// C++ has a `new`ed pointer it owns, `Box<dyn Node>` where it has a virtual
// call, and ordinary indexing where it walks an arena.
//
// That last one is not the same code, and the README says so beside the
// number: the C++ chases the arena through raw pointer arithmetic with no
// bounds check, and the safe Rust pays a compare and a branch per step
// because the index comes out of the array and the compiler cannot prove it
// is in range. Writing it with get_unchecked would remove the difference and
// would measure a Rust nobody writes.

use std::env;

struct Cell {
    value: i64,
}

fn managed_deref() -> i64 {
    let mut value = Box::new(Cell { value: 1 });
    let mut checksum: i64 = 0;
    for iteration in 0..50_000_000i32 {
        let next = (value.value * 1_664_525 + 1_013_904_223 + iteration as i64) & 2_147_483_647;
        value.value = next;
        checksum = (checksum + next) & 2_147_483_647;
    }
    checksum
}

fn heap_nodes() -> i64 {
    const SIZE: usize = 131_072;
    const MASK: u32 = SIZE as u32 - 1;
    let mut storage: Vec<Box<Cell>> = Vec::with_capacity(SIZE);
    for index in 0..SIZE {
        storage.push(Box::new(Cell {
            value: (index as i64) * 17 + 3,
        }));
    }

    let mut state: u32 = 123_456_789;
    let mut checksum: i64 = 0;
    for iteration in 0..20_000_000i32 {
        state = state.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
        let selected = (state & MASK) as usize;
        let node = &mut storage[selected];
        let next = (node.value + selected as i64 + (iteration & 1023) as i64) & 2_147_483_647;
        node.value = next;
        checksum = (checksum + next) & 2_147_483_647;
    }
    checksum
}

fn arena_graph() -> i64 {
    const SIZE: usize = 1_048_576;
    const MASK: usize = SIZE - 1;
    let mut order: Vec<i32> = vec![0; SIZE];
    let mut next: Vec<i32> = vec![0; SIZE];
    let mut values: Vec<i64> = vec![0; SIZE];
    for index in 0..SIZE {
        order[index] = index as i32;
        values[index] = (index as i64) * 17 + 3;
    }

    let mut state: u32 = 123_456_789;
    let mut index = SIZE - 1;
    while index > 0 {
        state = state.wrapping_mul(1_103_515_245).wrapping_add(12_345);
        let selected = ((state & 2_147_483_647) as usize) % (index + 1);
        order.swap(index, selected);
        index -= 1;
    }
    for index in 0..SIZE {
        next[order[index] as usize] = order[(index + 1) & MASK];
    }

    let mut current = order[0] as usize;
    let mut checksum: i64 = 0;
    for iteration in 0..5_000_000i32 {
        current = next[current] as usize;
        let value = values[current];
        checksum = (checksum + value + iteration as i64) & 2_147_483_647;
    }
    checksum
}

trait Node {
    fn evaluate(&self, round: i32) -> i64;
}

struct Leaf {
    value: i64,
}

impl Node for Leaf {
    fn evaluate(&self, round: i32) -> i64 {
        (self.value + round as i64) & 2_147_483_647
    }
}

struct AddNode {
    left: Box<dyn Node>,
    right: Box<dyn Node>,
    bias: i64,
}

impl Node for AddNode {
    fn evaluate(&self, round: i32) -> i64 {
        (self.left.evaluate(round) * 3 + self.right.evaluate(round) * 5 + self.bias + round as i64)
            & 2_147_483_647
    }
}

struct XorNode {
    left: Box<dyn Node>,
    right: Box<dyn Node>,
    bias: i64,
}

impl Node for XorNode {
    fn evaluate(&self, round: i32) -> i64 {
        (self.left.evaluate(round) ^ (self.right.evaluate(round) * 9 + self.bias + round as i64))
            & 2_147_483_647
    }
}

fn build_tree(depth: i32, seed: u32) -> Box<dyn Node> {
    if depth == 0 {
        return Box::new(Leaf {
            value: (seed & 2_147_483_647) as i64,
        });
    }
    let left_seed = seed.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
    let right_seed = left_seed.wrapping_mul(22_695_477).wrapping_add(1);
    let left = build_tree(depth - 1, left_seed);
    let right = build_tree(depth - 1, right_seed);
    let bias = ((seed >> 8) & 1023) as i64;
    if ((seed ^ depth as u32) & 1) == 0 {
        Box::new(AddNode { left, right, bias })
    } else {
        Box::new(XorNode { left, right, bias })
    }
}

fn object_tree() -> i64 {
    let root = build_tree(16, 123_456_789);
    let mut checksum: i64 = 0;
    for round in 0..200i32 {
        checksum = (checksum + root.evaluate(round)) & 2_147_483_647;
    }
    checksum
}

fn main() {
    let arguments: Vec<String> = env::args().collect();
    if arguments.len() != 2 {
        std::process::exit(2);
    }
    match arguments[1].as_str() {
        "managed-deref" => println!("{}", managed_deref()),
        "heap-nodes" => println!("{}", heap_nodes()),
        "arena-graph" => println!("{}", arena_graph()),
        "object-tree" => println!("{}", object_tree()),
        _ => std::process::exit(2),
    }
}
