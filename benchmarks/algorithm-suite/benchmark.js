function mix() {
    let checksum = 0;
    for (let round = 0; round < 5; ++round) {
        let state = (123456789 + round) | 0;
        for (let i = 0; i < 20000000; ++i) {
            state = (Math.imul(state, 1664525) + 1013904223) | 0;
            state = (state ^ (state >> 16)) | 0;
        }
        checksum = (checksum ^ state) | 0;
    }
    return checksum;
}

function primes() {
    let count = 1;
    for (let candidate = 3; candidate <= 2000000; candidate += 2) {
        let isPrime = true;
        for (let divisor = 3; divisor * divisor <= candidate; divisor += 2) {
            if (candidate % divisor === 0) {
                isPrime = false;
                break;
            }
        }
        if (isPrime) ++count;
    }
    return count;
}

function collatz() {
    let totalSteps = 0;
    for (let seed = 1; seed <= 2000000; ++seed) {
        let value = seed;
        while (value > 1) {
            value = value % 2 === 0 ? value / 2 : value * 3 + 1;
            ++totalSteps;
        }
    }
    return totalSteps;
}

function gcd() {
    let checksum = 0;
    for (let i = 1; i <= 5000000; ++i) {
        let left = i * 17 + 12345;
        let right = i * 31 + 6789;
        while (right !== 0) {
            const remainder = left % right;
            left = right;
            right = remainder;
        }
        checksum += left;
    }
    return checksum;
}

function floatingPoint() {
    let value = 0.5;
    let sum = 0.0;
    for (let i = 0; i < 100000000; ++i) {
        value = value * 1.000000119 + 0.0000001;
        if (value > 2.0) value -= 1.5;
        sum += value;
    }
    return Math.trunc(sum);
}

const algorithms = { mix, primes, collatz, gcd, floating: floatingPoint };
const algorithm = algorithms[process.argv[2]];
if (!algorithm) process.exit(2);
console.log(algorithm());
