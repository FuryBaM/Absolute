import sys


MASK = 0xFFFFFFFF
SIGN = 0x80000000
MODULUS = 0x100000000


def int32(value):
    value &= MASK
    return value - MODULUS if value >= SIGN else value


def scan():
    size = 1000000
    values = [index * 17 + 3 for index in range(size)]
    checksum = 0
    for round_index in range(100):
        for index in range(size):
            values[index] = int32(values[index] * 3 + round_index)
            checksum += values[index]
    return checksum


def random_access():
    size = 1048576
    mask = size - 1
    values = [index * 31 + 7 for index in range(size)]
    state = 123456789
    checksum = 0
    for _ in range(100000000):
        state = int32(state * 1664525 + 1013904223)
        index = state & mask
        values[index] = int32(values[index] ^ state)
        checksum += values[index]
    return checksum


def insertion_sort():
    size = 30000
    values = [0] * size
    state = 123456789
    for index in range(size):
        state = int32(state * 1664525 + 1013904223)
        values[index] = state
    for index in range(1, size):
        key = values[index]
        position = index - 1
        while position >= 0:
            if values[position] <= key:
                break
            values[position + 1] = values[position]
            position -= 1
        values[position + 1] = key
    checksum = sum(values)
    checksum += values[0] * 31
    checksum += values[-1] * 17
    return checksum


ALGORITHMS = {
    "scan": scan,
    "random-access": random_access,
    "insertion-sort": insertion_sort,
}

if len(sys.argv) != 2 or sys.argv[1] not in ALGORITHMS:
    raise SystemExit(2)
print(ALGORITHMS[sys.argv[1]]())
