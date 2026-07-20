public final class Benchmark {
    static long scan() {
        int size = 1000000;
        int[] values = new int[size];
        for (int index = 0; index < size; ++index) values[index] = index * 17 + 3;
        long checksum = 0;
        for (int round = 0; round < 100; ++round) {
            for (int index = 0; index < size; ++index) {
                values[index] = values[index] * 3 + round;
                checksum += values[index];
            }
        }
        return checksum;
    }

    static long randomAccess() {
        int size = 1048576;
        int mask = size - 1;
        int[] values = new int[size];
        for (int index = 0; index < size; ++index) values[index] = index * 31 + 7;
        int state = 123456789;
        long checksum = 0;
        for (int iteration = 0; iteration < 100000000; ++iteration) {
            state = state * 1664525 + 1013904223;
            int index = state & mask;
            values[index] ^= state;
            checksum += values[index];
        }
        return checksum;
    }

    static long insertionSort() {
        int size = 30000;
        int[] values = new int[size];
        int state = 123456789;
        for (int index = 0; index < size; ++index) {
            state = state * 1664525 + 1013904223;
            values[index] = state;
        }
        for (int index = 1; index < size; ++index) {
            int key = values[index];
            int position = index - 1;
            while (position >= 0) {
                if (values[position] <= key) break;
                values[position + 1] = values[position];
                --position;
            }
            values[position + 1] = key;
        }
        long checksum = 0;
        for (int value : values) checksum += value;
        checksum += (long)values[0] * 31;
        checksum += (long)values[size - 1] * 17;
        return checksum;
    }

    public static void main(String[] args) {
        if (args.length != 1) System.exit(2);
        switch (args[0]) {
            case "scan" -> System.out.println(scan());
            case "random-access" -> System.out.println(randomAccess());
            case "insertion-sort" -> System.out.println(insertionSort());
            default -> System.exit(2);
        }
    }
}
