public final class Benchmark {
    static int mix() {
        int checksum = 0;
        for (int round = 0; round < 5; ++round) {
            int state = 123456789 + round;
            for (int i = 0; i < 20000000; ++i) {
                state = state * 1664525 + 1013904223;
                state ^= state >> 16;
            }
            checksum ^= state;
        }
        return checksum;
    }

    static int primes() {
        int count = 1;
        for (int candidate = 3; candidate <= 2000000; candidate += 2) {
            boolean isPrime = true;
            for (int divisor = 3; divisor * divisor <= candidate; divisor += 2) {
                if (candidate % divisor == 0) {
                    isPrime = false;
                    break;
                }
            }
            if (isPrime) ++count;
        }
        return count;
    }

    static long collatz() {
        long totalSteps = 0;
        for (int seed = 1; seed <= 2000000; ++seed) {
            long value = seed;
            while (value > 1) {
                value = value % 2 == 0 ? value / 2 : value * 3 + 1;
                ++totalSteps;
            }
        }
        return totalSteps;
    }

    static long gcd() {
        long checksum = 0;
        for (int i = 1; i <= 5000000; ++i) {
            int left = i * 17 + 12345;
            int right = i * 31 + 6789;
            while (right != 0) {
                int remainder = left % right;
                left = right;
                right = remainder;
            }
            checksum += left;
        }
        return checksum;
    }

    static int floatingPoint() {
        double value = 0.5;
        double sum = 0.0;
        for (int i = 0; i < 100000000; ++i) {
            value = value * 1.000000119 + 0.0000001;
            if (value > 2.0) value -= 1.5;
            sum += value;
        }
        return (int)sum;
    }

    public static void main(String[] args) {
        if (args.length != 1) System.exit(2);
        switch (args[0]) {
            case "mix" -> System.out.println(mix());
            case "primes" -> System.out.println(primes());
            case "collatz" -> System.out.println(collatz());
            case "gcd" -> System.out.println(gcd());
            case "floating" -> System.out.println(floatingPoint());
            default -> System.exit(2);
        }
    }
}
