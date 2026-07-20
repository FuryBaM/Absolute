let checksum = 0;

for (let round = 0; round < 5; ++round) {
    let state = (123456789 + round) | 0;
    for (let i = 0; i < 20000000; ++i) {
        state = (Math.imul(state, 1664525) + 1013904223) | 0;
        state = (state ^ (state >> 16)) | 0;
    }
    checksum = (checksum ^ state) | 0;
}

console.log(checksum);
