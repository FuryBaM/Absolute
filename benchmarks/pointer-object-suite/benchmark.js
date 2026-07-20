'use strict';

const MASK = 0x7fffffff;

class Box {
    constructor(value) { this.value = value; }
}

class Node {
    evaluate(_round) { throw new Error('abstract'); }
}

class Leaf extends Node {
    constructor(seed) {
        super();
        this.value = seed & MASK;
    }
    evaluate(round) { return (this.value + round) & MASK; }
}

class BinaryNode extends Node {
    constructor(left, right, seed) {
        super();
        this.left = left;
        this.right = right;
        this.bias = (seed >>> 8) & 1023;
    }
}

class AddNode extends BinaryNode {
    evaluate(round) {
        return (this.left.evaluate(round) * 3 + this.right.evaluate(round) * 5 +
            this.bias + round) & MASK;
    }
}

class XorNode extends BinaryNode {
    evaluate(round) {
        return (this.left.evaluate(round) ^
            (this.right.evaluate(round) * 9 + this.bias + round)) & MASK;
    }
}

function managedDeref() {
    const value = new Box(1);
    let checksum = 0;
    for (let iteration = 0; iteration < 50000000; ++iteration) {
        const next = (value.value * 1664525 + 1013904223 + iteration) & MASK;
        value.value = next;
        checksum = (checksum + next) & MASK;
    }
    return checksum;
}

function heapNodes() {
    const size = 131072;
    const mask = size - 1;
    const nodes = new Array(size);
    for (let index = 0; index < size; ++index)
        nodes[index] = new Box(index * 17 + 3);

    let state = 123456789;
    let checksum = 0;
    for (let iteration = 0; iteration < 20000000; ++iteration) {
        state = (Math.imul(state, 1664525) + 1013904223) | 0;
        const selected = state & mask;
        const node = nodes[selected];
        const next = (node.value + selected + (iteration & 1023)) & MASK;
        node.value = next;
        checksum = (checksum + next) & MASK;
    }
    return checksum;
}

function arenaGraph() {
    const size = 1048576;
    const mask = size - 1;
    const order = new Int32Array(size);
    const next = new Int32Array(size);
    const values = new Float64Array(size);
    for (let index = 0; index < size; ++index) {
        order[index] = index;
        values[index] = index * 17 + 3;
    }

    let state = 123456789;
    for (let index = size - 1; index > 0; --index) {
        state = (Math.imul(state, 1103515245) + 12345) | 0;
        const selected = (state & MASK) % (index + 1);
        const temporary = order[index];
        order[index] = order[selected];
        order[selected] = temporary;
    }
    for (let index = 0; index < size; ++index)
        next[order[index]] = order[(index + 1) & mask];

    let current = order[0];
    let checksum = 0;
    for (let iteration = 0; iteration < 5000000; ++iteration) {
        current = next[current];
        checksum = (checksum + values[current] + iteration) & MASK;
    }
    return checksum;
}

function buildTree(depth, seed) {
    if (depth === 0) return new Leaf(seed);
    const leftSeed = (Math.imul(seed, 1664525) + 1013904223) | 0;
    const rightSeed = (Math.imul(leftSeed, 22695477) + 1) | 0;
    const left = buildTree(depth - 1, leftSeed);
    const right = buildTree(depth - 1, rightSeed);
    return ((seed ^ depth) & 1) === 0
        ? new AddNode(left, right, seed)
        : new XorNode(left, right, seed);
}

function objectTree() {
    const root = buildTree(16, 123456789);
    let checksum = 0;
    for (let round = 0; round < 200; ++round)
        checksum = (checksum + root.evaluate(round)) & MASK;
    return checksum;
}

let result;
switch (process.argv[2]) {
    case 'managed-deref': result = managedDeref(); break;
    case 'heap-nodes': result = heapNodes(); break;
    case 'arena-graph': result = arenaGraph(); break;
    case 'object-tree': result = objectTree(); break;
    default: process.exit(2);
}
console.log(result);
