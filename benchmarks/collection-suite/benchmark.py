import sys


def vector_push_sum():
    N = 1_000_000
    lst = []
    for i in range(N):
        lst.append(i * 17 + 3)
    checksum = sum(lst)
    for _ in range(500_000):
        lst.pop()
    checksum += len(lst)
    return checksum


def vector_sort():
    SIZE = 100_000
    mask = 0xFFFFFFFF
    state = 123456789
    values = []
    for _ in range(SIZE):
        state = (state * 1664525 + 1013904223) & mask
        # sign-extend to int32
        v = state if state < 0x80000000 else state - 0x100000000
        values.append(v)
    # insertion sort
    for i in range(1, SIZE):
        key = values[i]
        j = i - 1
        while j >= 0 and values[j] > key:
            values[j + 1] = values[j]
            j -= 1
        values[j + 1] = key
    checksum = sum(values)
    checksum += values[0] * 31
    checksum += values[SIZE - 1] * 17
    return checksum


def hashmap_insert_lookup():
    N = 200_000
    d = {}
    for i in range(N):
        k = i * 17 + 31
        v = i * 13 + 7
        d[k] = v
    checksum = 0
    for i in range(N):
        k = i * 17 + 31
        if k in d:
            checksum += d[k]
    return checksum


BENCHMARKS = {
    'vector-push-sum': vector_push_sum,
    'vector-sort': vector_sort,
    'hashmap-insert-lookup': hashmap_insert_lookup,
}

if len(sys.argv) != 2 or sys.argv[1] not in BENCHMARKS:
    raise SystemExit(2)
print(BENCHMARKS[sys.argv[1]]())
