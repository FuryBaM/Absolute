int checksum = 0;

for (int round = 0; round < 5; ++round)
{
    int state = 123456789 + round;
    for (int i = 0; i < 20000000; ++i)
    {
        state = unchecked(state * 1664525 + 1013904223);
        state ^= state >> 16;
    }
    checksum ^= state;
}

Console.WriteLine(checksum);
