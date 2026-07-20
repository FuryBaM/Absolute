using System;

sealed class Box
{
    public long Value;
    public Box(long value) { Value = value; }
}

abstract class Node
{
    public abstract long Evaluate(int round);
}

sealed class Leaf : Node
{
    private readonly long value;
    public Leaf(uint seed) { value = seed & 2147483647u; }
    public override long Evaluate(int round) => (value + round) & 2147483647L;
}

abstract class BinaryNode : Node
{
    protected readonly Node Left;
    protected readonly Node Right;
    protected readonly long Bias;

    protected BinaryNode(Node left, Node right, uint seed)
    {
        Left = left;
        Right = right;
        Bias = (seed >> 8) & 1023u;
    }
}

sealed class AddNode : BinaryNode
{
    public AddNode(Node left, Node right, uint seed) : base(left, right, seed) { }
    public override long Evaluate(int round) =>
        (Left.Evaluate(round) * 3 + Right.Evaluate(round) * 5 + Bias + round) & 2147483647L;
}

sealed class XorNode : BinaryNode
{
    public XorNode(Node left, Node right, uint seed) : base(left, right, seed) { }
    public override long Evaluate(int round) =>
        (Left.Evaluate(round) ^ (Right.Evaluate(round) * 9 + Bias + round)) & 2147483647L;
}

static class Program
{
    private static readonly Box[] HeapStorage = new Box[131072];
    private static readonly int[] ArenaOrder = new int[1048576];
    private static readonly int[] ArenaNext = new int[1048576];
    private static readonly long[] ArenaValues = new long[1048576];

    private static long ManagedDeref()
    {
        Box value = new Box(1);
        long checksum = 0;
        for (int iteration = 0; iteration < 50000000; ++iteration)
        {
            long next = (value.Value * 1664525 + 1013904223 + iteration) & 2147483647L;
            value.Value = next;
            checksum = (checksum + next) & 2147483647L;
        }
        return checksum;
    }

    private static long HeapNodes()
    {
        const int size = 131072;
        const int mask = size - 1;
        for (int index = 0; index < size; ++index)
            HeapStorage[index] = new Box(index * 17 + 3);

        uint state = 123456789u;
        long checksum = 0;
        for (int iteration = 0; iteration < 20000000; ++iteration)
        {
            state = unchecked(state * 1664525u + 1013904223u);
            int selected = (int)(state & mask);
            Box node = HeapStorage[selected];
            long next = (node.Value + selected + (iteration & 1023)) & 2147483647L;
            node.Value = next;
            checksum = (checksum + next) & 2147483647L;
        }
        return checksum;
    }

    private static long ArenaGraph()
    {
        const int size = 1048576;
        const int mask = size - 1;
        for (int index = 0; index < size; ++index)
        {
            ArenaOrder[index] = index;
            ArenaValues[index] = index * 17L + 3;
        }

        uint state = 123456789u;
        for (int index = size - 1; index > 0; --index)
        {
            state = unchecked(state * 1103515245u + 12345u);
            int selected = (int)(state & 2147483647u) % (index + 1);
            int temporary = ArenaOrder[index];
            ArenaOrder[index] = ArenaOrder[selected];
            ArenaOrder[selected] = temporary;
        }
        for (int index = 0; index < size; ++index)
            ArenaNext[ArenaOrder[index]] = ArenaOrder[(index + 1) & mask];

        int current = ArenaOrder[0];
        long checksum = 0;
        for (int iteration = 0; iteration < 5000000; ++iteration)
        {
            current = ArenaNext[current];
            checksum = (checksum + ArenaValues[current] + iteration) & 2147483647L;
        }
        return checksum;
    }

    private static Node BuildTree(int depth, uint seed)
    {
        if (depth == 0) return new Leaf(seed);
        uint leftSeed = unchecked(seed * 1664525u + 1013904223u);
        uint rightSeed = unchecked(leftSeed * 22695477u + 1u);
        Node left = BuildTree(depth - 1, leftSeed);
        Node right = BuildTree(depth - 1, rightSeed);
        return ((seed ^ (uint)depth) & 1u) == 0
            ? new AddNode(left, right, seed)
            : new XorNode(left, right, seed);
    }

    private static long ObjectTree()
    {
        Node root = BuildTree(16, 123456789u);
        long checksum = 0;
        for (int round = 0; round < 200; ++round)
            checksum = (checksum + root.Evaluate(round)) & 2147483647L;
        return checksum;
    }

    public static int Main(string[] args)
    {
        if (args.Length != 1) return 2;
        long result;
        switch (args[0])
        {
            case "managed-deref": result = ManagedDeref(); break;
            case "heap-nodes": result = HeapNodes(); break;
            case "arena-graph": result = ArenaGraph(); break;
            case "object-tree": result = ObjectTree(); break;
            default: return 2;
        }
        Console.WriteLine(result);
        return 0;
    }
}
