'use strict';
const arg = process.argv[2];

function vectorPushSum() {
    const N = 1_000_000;
    const list = [];
    for (let i = 0; i < N; i++) list.push(i * 17 + 3);
    let checksum = BigInt(0);
    for (let i = 0; i < N; i++) checksum += BigInt(list[i]);
    for (let i = 0; i < 500_000; i++) list.pop();
    checksum += BigInt(list.length);
    return checksum;
}

function vectorSort() {
    const SIZE = 100_000;
    const values = new Int32Array(SIZE);
    let state = 123456789 >>> 0;
    for (let i = 0; i < SIZE; i++) {
        state = Math.imul(state, 1664525) + 1013904223 >>> 0;
        values[i] = state | 0;
    }
    // insertion sort
    for (let i = 1; i < SIZE; i++) {
        const key = values[i];
        let j = i - 1;
        while (j >= 0 && values[j] > key) { values[j + 1] = values[j]; j--; }
        values[j + 1] = key;
    }
    let checksum = BigInt(0);
    for (let i = 0; i < SIZE; i++) checksum += BigInt(values[i]);
    checksum += BigInt(values[0]) * BigInt(31);
    checksum += BigInt(values[SIZE - 1]) * BigInt(17);
    return checksum;
}

function hashmapInsertLookup() {
    const N = 200_000;
    const map = new Map();
    for (let i = 0; i < N; i++) {
        const k = i * 17 + 31;
        const v = i * 13 + 7;
        map.set(k, v);
    }
    let checksum = BigInt(0);
    for (let i = 0; i < N; i++) {
        const k = i * 17 + 31;
        const v = map.get(k);
        if (v !== undefined) checksum += BigInt(v);
    }
    return checksum;
}

let result;
if (arg === 'vector-push-sum') result = vectorPushSum();
else if (arg === 'vector-sort') result = vectorSort();
else if (arg === 'hashmap-insert-lookup') result = hashmapInsertLookup();
else process.exit(2);

console.log(result.toString());
