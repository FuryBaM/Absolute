static int Mix()
{
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
    return checksum;
}

static int Primes()
{
    int count = 1;
    for (int candidate = 3; candidate <= 2000000; candidate += 2)
    {
        bool isPrime = true;
        for (int divisor = 3; divisor * divisor <= candidate; divisor += 2)
        {
            if (candidate % divisor == 0)
            {
                isPrime = false;
                break;
            }
        }
        if (isPrime) ++count;
    }
    return count;
}

static long Collatz()
{
    long totalSteps = 0;
    for (int seed = 1; seed <= 2000000; ++seed)
    {
        long value = seed;
        while (value > 1)
        {
            value = value % 2 == 0 ? value / 2 : value * 3 + 1;
            ++totalSteps;
        }
    }
    return totalSteps;
}

static long Gcd()
{
    long checksum = 0;
    for (int i = 1; i <= 5000000; ++i)
    {
        int left = i * 17 + 12345;
        int right = i * 31 + 6789;
        while (right != 0)
        {
            int remainder = left % right;
            left = right;
            right = remainder;
        }
        checksum += left;
    }
    return checksum;
}

static int FloatingPoint()
{
    double value = 0.5;
    double sum = 0.0;
    for (int i = 0; i < 100000000; ++i)
    {
        value = value * 1.000000119 + 0.0000001;
        if (value > 2.0) value -= 1.5;
        sum += value;
    }
    return (int)sum;
}

if (args.Length != 1) return 2;
switch (args[0])
{
    case "mix": Console.WriteLine(Mix()); break;
    case "primes": Console.WriteLine(Primes()); break;
    case "collatz": Console.WriteLine(Collatz()); break;
    case "gcd": Console.WriteLine(Gcd()); break;
    case "floating": Console.WriteLine(FloatingPoint()); break;
    default: return 2;
}
return 0;
