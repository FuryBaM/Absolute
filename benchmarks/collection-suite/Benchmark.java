import java.util.*;

public class Benchmark {
    static long vectorPushSum() {
        int N = 1_000_000;
        ArrayList<Integer> list = new ArrayList<>(N);
        for (int i = 0; i < N; i++) list.add(i * 17 + 3);
        long checksum = 0;
        for (int i = 0; i < N; i++) checksum += list.get(i);
        for (int i = 0; i < 500_000; i++) list.remove(list.size() - 1);
        checksum += list.size();
        return checksum;
    }

    static long vectorSort() {
        int SIZE = 100_000;
        ArrayList<Integer> values = new ArrayList<>(SIZE);
        int state = 123456789;
        for (int i = 0; i < SIZE; i++) {
            state = state * 1664525 + 1013904223;
            values.add(state);
        }
        // insertion sort
        for (int i = 1; i < SIZE; i++) {
            int key = values.get(i);
            int j = i - 1;
            while (j >= 0 && values.get(j) > key) {
                values.set(j + 1, values.get(j));
                j--;
            }
            values.set(j + 1, key);
        }
        long checksum = 0;
        for (int i = 0; i < SIZE; i++) checksum += values.get(i);
        checksum += (long) values.get(0) * 31;
        checksum += (long) values.get(SIZE - 1) * 17;
        return checksum;
    }

    static long hashmapInsertLookup() {
        int N = 200_000;
        HashMap<Integer, Integer> map = new HashMap<>(N * 2);
        for (int i = 0; i < N; i++) {
            int k = i * 17 + 31;
            int v = i * 13 + 7;
            map.put(k, v);
        }
        long checksum = 0;
        for (int i = 0; i < N; i++) {
            int k = i * 17 + 31;
            Integer v = map.get(k);
            if (v != null) checksum += v;
        }
        return checksum;
    }

    public static void main(String[] args) {
        if (args.length != 1) { System.exit(2); }
        switch (args[0]) {
            case "vector-push-sum": System.out.println(vectorPushSum()); break;
            case "vector-sort": System.out.println(vectorSort()); break;
            case "hashmap-insert-lookup": System.out.println(hashmapInsertLookup()); break;
            default: System.exit(2);
        }
    }
}
