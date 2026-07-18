function scan() {
    const size = 1000000;
    const values = new Int32Array(size);
    for (let index = 0; index < size; ++index) values[index] = index * 17 + 3;
    let checksum = 0;
    for (let round = 0; round < 100; ++round) {
        for (let index = 0; index < size; ++index) {
            values[index] = Math.imul(values[index], 3) + round;
            checksum += values[index];
        }
    }
    return checksum;
}

function randomAccess() {
    const size = 1048576;
    const mask = size - 1;
    const values = new Int32Array(size);
    for (let index = 0; index < size; ++index) values[index] = index * 31 + 7;
    let state = 123456789;
    let checksum = 0;
    for (let iteration = 0; iteration < 100000000; ++iteration) {
        state = (Math.imul(state, 1664525) + 1013904223) | 0;
        const index = state & mask;
        values[index] ^= state;
        checksum += values[index];
    }
    return checksum;
}

function insertionSort() {
    const size = 30000;
    const values = new Int32Array(size);
    let state = 123456789;
    for (let index = 0; index < size; ++index) {
        state = (Math.imul(state, 1664525) + 1013904223) | 0;
        values[index] = state;
    }
    for (let index = 1; index < size; ++index) {
        const key = values[index];
        let position = index - 1;
        while (position >= 0) {
            if (values[position] <= key) break;
            values[position + 1] = values[position];
            --position;
        }
        values[position + 1] = key;
    }
    let checksum = 0;
    for (const value of values) checksum += value;
    checksum += values[0] * 31;
    checksum += values[size - 1] * 17;
    return checksum;
}

const algorithms = { scan, "random-access": randomAccess, "insertion-sort": insertionSort };
const algorithm = algorithms[process.argv[2]];
if (!algorithm) process.exit(2);
console.log(algorithm());
