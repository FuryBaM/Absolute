import sys

MASK = 0x7FFFFFFF


class Box:
    __slots__ = ("value",)

    def __init__(self, value):
        self.value = value


class Node:
    __slots__ = ()

    def evaluate(self, round_number):
        raise NotImplementedError


class Leaf(Node):
    __slots__ = ("value",)

    def __init__(self, seed):
        self.value = seed & MASK

    def evaluate(self, round_number):
        return (self.value + round_number) & MASK


class BinaryNode(Node):
    __slots__ = ("left", "right", "bias")

    def __init__(self, left, right, seed):
        self.left = left
        self.right = right
        self.bias = (seed >> 8) & 1023


class AddNode(BinaryNode):
    __slots__ = ()

    def evaluate(self, round_number):
        return (
            self.left.evaluate(round_number) * 3
            + self.right.evaluate(round_number) * 5
            + self.bias
            + round_number
        ) & MASK


class XorNode(BinaryNode):
    __slots__ = ()

    def evaluate(self, round_number):
        return (
            self.left.evaluate(round_number)
            ^ (self.right.evaluate(round_number) * 9 + self.bias + round_number)
        ) & MASK


def managed_deref():
    value = Box(1)
    checksum = 0
    for iteration in range(50_000_000):
        next_value = (value.value * 1664525 + 1013904223 + iteration) & MASK
        value.value = next_value
        checksum = (checksum + next_value) & MASK
    return checksum


def heap_nodes():
    size = 131072
    mask = size - 1
    nodes = [Box(index * 17 + 3) for index in range(size)]
    state = 123456789
    checksum = 0
    for iteration in range(20_000_000):
        state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
        selected = state & mask
        node = nodes[selected]
        next_value = (node.value + selected + (iteration & 1023)) & MASK
        node.value = next_value
        checksum = (checksum + next_value) & MASK
    return checksum


def arena_graph():
    size = 1048576
    mask = size - 1
    order = list(range(size))
    following = [0] * size
    values = [index * 17 + 3 for index in range(size)]
    state = 123456789
    for index in range(size - 1, 0, -1):
        state = (state * 1103515245 + 12345) & 0xFFFFFFFF
        selected = (state & MASK) % (index + 1)
        order[index], order[selected] = order[selected], order[index]
    for index in range(size):
        following[order[index]] = order[(index + 1) & mask]

    current = order[0]
    checksum = 0
    for iteration in range(5_000_000):
        current = following[current]
        checksum = (checksum + values[current] + iteration) & MASK
    return checksum


def build_tree(depth, seed):
    if depth == 0:
        return Leaf(seed)
    left_seed = (seed * 1664525 + 1013904223) & 0xFFFFFFFF
    right_seed = (left_seed * 22695477 + 1) & 0xFFFFFFFF
    left = build_tree(depth - 1, left_seed)
    right = build_tree(depth - 1, right_seed)
    if ((seed ^ depth) & 1) == 0:
        return AddNode(left, right, seed)
    return XorNode(left, right, seed)


def object_tree():
    root = build_tree(16, 123456789)
    checksum = 0
    for round_number in range(200):
        checksum = (checksum + root.evaluate(round_number)) & MASK
    return checksum


ALGORITHMS = {
    "managed-deref": managed_deref,
    "heap-nodes": heap_nodes,
    "arena-graph": arena_graph,
    "object-tree": object_tree,
}

if len(sys.argv) != 2 or sys.argv[1] not in ALGORITHMS:
    raise SystemExit(2)

print(ALGORITHMS[sys.argv[1]]())
