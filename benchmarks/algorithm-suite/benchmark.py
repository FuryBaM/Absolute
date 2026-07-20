import sys


def mix():
    mask = 0xFFFFFFFF
    sign = 0x80000000
    modulus = 0x100000000
    checksum = 0
    for round_index in range(5):
        state = 123456789 + round_index
        for _ in range(20000000):
            state = (state * 1664525 + 1013904223) & mask
            if state >= sign:
                state -= modulus
            state = (state ^ (state >> 16)) & mask
            if state >= sign:
                state -= modulus
        checksum = (checksum ^ state) & mask
        if checksum >= sign:
            checksum -= modulus
    return checksum


def primes():
    count = 1
    for candidate in range(3, 2000001, 2):
        is_prime = True
        divisor = 3
        while divisor * divisor <= candidate:
            if candidate % divisor == 0:
                is_prime = False
                break
            divisor += 2
        if is_prime:
            count += 1
    return count


def collatz():
    total_steps = 0
    for seed in range(1, 2000001):
        value = seed
        while value > 1:
            value = value // 2 if value % 2 == 0 else value * 3 + 1
            total_steps += 1
    return total_steps


def gcd():
    checksum = 0
    for index in range(1, 5000001):
        left = index * 17 + 12345
        right = index * 31 + 6789
        while right != 0:
            left, right = right, left % right
        checksum += left
    return checksum


def floating_point():
    value = 0.5
    total = 0.0
    for _ in range(100000000):
        value = value * 1.000000119 + 0.0000001
        if value > 2.0:
            value -= 1.5
        total += value
    return int(total)


ALGORITHMS = {
    "mix": mix,
    "primes": primes,
    "collatz": collatz,
    "gcd": gcd,
    "floating": floating_point,
}

if len(sys.argv) != 2 or sys.argv[1] not in ALGORITHMS:
    raise SystemExit(2)
print(ALGORITHMS[sys.argv[1]]())
