public final class Benchmark {
    private static final long MASK = 2147483647L;

    private static final class Box {
        long value;
        Box(long value) { this.value = value; }
    }

    private abstract static class Node {
        abstract long evaluate(int round);
    }

    private static final class Leaf extends Node {
        private final long value;
        Leaf(int seed) { value = Integer.toUnsignedLong(seed) & MASK; }
        @Override long evaluate(int round) { return (value + round) & MASK; }
    }

    private abstract static class BinaryNode extends Node {
        final Node left;
        final Node right;
        final long bias;
        BinaryNode(Node left, Node right, int seed) {
            this.left = left;
            this.right = right;
            this.bias = (seed >>> 8) & 1023;
        }
    }

    private static final class AddNode extends BinaryNode {
        AddNode(Node left, Node right, int seed) { super(left, right, seed); }
        @Override long evaluate(int round) {
            return (left.evaluate(round) * 3 + right.evaluate(round) * 5 + bias + round) & MASK;
        }
    }

    private static final class XorNode extends BinaryNode {
        XorNode(Node left, Node right, int seed) { super(left, right, seed); }
        @Override long evaluate(int round) {
            return (left.evaluate(round) ^ (right.evaluate(round) * 9 + bias + round)) & MASK;
        }
    }

    private static final Box[] HEAP_STORAGE = new Box[131072];
    private static final int[] ARENA_ORDER = new int[1048576];
    private static final int[] ARENA_NEXT = new int[1048576];
    private static final long[] ARENA_VALUES = new long[1048576];

    private static long managedDeref() {
        Box value = new Box(1);
        long checksum = 0;
        for (int iteration = 0; iteration < 50000000; ++iteration) {
            long next = (value.value * 1664525 + 1013904223 + iteration) & MASK;
            value.value = next;
            checksum = (checksum + next) & MASK;
        }
        return checksum;
    }

    private static long heapNodes() {
        final int size = 131072;
        final int mask = size - 1;
        for (int index = 0; index < size; ++index)
            HEAP_STORAGE[index] = new Box(index * 17L + 3);

        int state = 123456789;
        long checksum = 0;
        for (int iteration = 0; iteration < 20000000; ++iteration) {
            state = state * 1664525 + 1013904223;
            int selected = state & mask;
            Box node = HEAP_STORAGE[selected];
            long next = (node.value + selected + (iteration & 1023)) & MASK;
            node.value = next;
            checksum = (checksum + next) & MASK;
        }
        return checksum;
    }

    private static long arenaGraph() {
        final int size = 1048576;
        final int mask = size - 1;
        for (int index = 0; index < size; ++index) {
            ARENA_ORDER[index] = index;
            ARENA_VALUES[index] = index * 17L + 3;
        }

        int state = 123456789;
        for (int index = size - 1; index > 0; --index) {
            state = state * 1103515245 + 12345;
            int selected = (state & 2147483647) % (index + 1);
            int temporary = ARENA_ORDER[index];
            ARENA_ORDER[index] = ARENA_ORDER[selected];
            ARENA_ORDER[selected] = temporary;
        }
        for (int index = 0; index < size; ++index)
            ARENA_NEXT[ARENA_ORDER[index]] = ARENA_ORDER[(index + 1) & mask];

        int current = ARENA_ORDER[0];
        long checksum = 0;
        for (int iteration = 0; iteration < 5000000; ++iteration) {
            current = ARENA_NEXT[current];
            checksum = (checksum + ARENA_VALUES[current] + iteration) & MASK;
        }
        return checksum;
    }

    private static Node buildTree(int depth, int seed) {
        if (depth == 0) return new Leaf(seed);
        int leftSeed = seed * 1664525 + 1013904223;
        int rightSeed = leftSeed * 22695477 + 1;
        Node left = buildTree(depth - 1, leftSeed);
        Node right = buildTree(depth - 1, rightSeed);
        return ((seed ^ depth) & 1) == 0
            ? new AddNode(left, right, seed)
            : new XorNode(left, right, seed);
    }

    private static long objectTree() {
        Node root = buildTree(16, 123456789);
        long checksum = 0;
        for (int round = 0; round < 200; ++round)
            checksum = (checksum + root.evaluate(round)) & MASK;
        return checksum;
    }

    public static void main(String[] args) {
        if (args.length != 1) System.exit(2);
        long result;
        switch (args[0]) {
            case "managed-deref": result = managedDeref(); break;
            case "heap-nodes": result = heapNodes(); break;
            case "arena-graph": result = arenaGraph(); break;
            case "object-tree": result = objectTree(); break;
            default: System.exit(2); return;
        }
        System.out.println(result);
    }
}
