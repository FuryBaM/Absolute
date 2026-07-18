MASK = 0xFFFFFFFF
SIGN = 0x80000000
MODULUS = 0x100000000

checksum = 0

for round_index in range(5):
    state = 123456789 + round_index
    for _ in range(20000000):
        state = (state * 1664525 + 1013904223) & MASK
        if state >= SIGN:
            state -= MODULUS
        state = (state ^ (state >> 16)) & MASK
        if state >= SIGN:
            state -= MODULUS
    checksum = (checksum ^ state) & MASK
    if checksum >= SIGN:
        checksum -= MODULUS

print(checksum)
