using System;
using System.Collections.Generic;

static long VectorPushSum()
{
    const int N = 1_000_000;
    var list = new List<int>(N);
    for (int i = 0; i < N; i++) list.Add(i * 17 + 3);
    long checksum = 0;
    for (int i = 0; i < N; i++) checksum += list[i];
    for (int i = 0; i < 500_000; i++) list.RemoveAt(list.Count - 1);
    checksum += list.Count;
    return checksum;
}

static long VectorSort()
{
    const int SIZE = 100_000;
    var values = new List<int>(SIZE);
    uint state = 123456789u;
    for (int i = 0; i < SIZE; i++)
    {
        state = unchecked(state * 1664525u + 1013904223u);
        values.Add(unchecked((int)state));
    }
    // insertion sort
    for (int i = 1; i < SIZE; i++)
    {
        int key = values[i];
        int j = i - 1;
        while (j >= 0 && values[j] > key) { values[j + 1] = values[j]; j--; }
        values[j + 1] = key;
    }
    long checksum = 0;
    for (int i = 0; i < SIZE; i++) checksum += values[i];
    checksum += (long)values[0] * 31;
    checksum += (long)values[SIZE - 1] * 17;
    return checksum;
}

static long HashmapInsertLookup()
{
    const int N = 200_000;
    var map = new Dictionary<int, int>(N * 2);
    for (int i = 0; i < N; i++)
    {
        int k = i * 17 + 31;
        int v = i * 13 + 7;
        map[k] = v;
    }
    long checksum = 0;
    for (int i = 0; i < N; i++)
    {
        int k = i * 17 + 31;
        if (map.TryGetValue(k, out int val)) checksum += val;
    }
    return checksum;
}

if (args.Length != 1) return 2;
switch (args[0])
{
    case "vector-push-sum": Console.WriteLine(VectorPushSum()); break;
    case "vector-sort": Console.WriteLine(VectorSort()); break;
    case "hashmap-insert-lookup": Console.WriteLine(HashmapInsertLookup()); break;
    default: return 2;
}
return 0;
