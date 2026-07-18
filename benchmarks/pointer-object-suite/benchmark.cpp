using int32 = int;
using uint32 = unsigned int;
using int64 = long long;
#if defined(_WIN64)
using size_type = unsigned long long;
#else
using size_type = unsigned long;
#endif

extern "C" int printf(const char*, ...);
extern "C" void* malloc(size_type);
extern "C" void free(void*);

void* operator new(size_type size) {
    return malloc(size);
}

void operator delete(void* pointer) noexcept {
    free(pointer);
}

void operator delete(void* pointer, size_type) noexcept {
    free(pointer);
}

bool equals(const char* left, const char* right) {
    while (*left != 0 && *left == *right) { ++left; ++right; }
    return *left == *right;
}

struct Box {
    int64 value;
    explicit Box(int64 initial) : value(initial) {}
};

int64 managedDeref() {
    Box* value = new Box(1);
    int64 checksum = 0;
    for (int32 iteration = 0; iteration < 50000000; ++iteration) {
        const int64 next = (value->value * 1664525 + 1013904223 + iteration) & 2147483647LL;
        value->value = next;
        checksum = (checksum + next) & 2147483647LL;
    }
    delete value;
    return checksum;
}

Box* heapStorage[131072];

int64 heapNodes() {
    constexpr int32 size = 131072;
    constexpr int32 mask = size - 1;
    for (int32 index = 0; index < size; ++index)
        heapStorage[index] = new Box(index * 17 + 3);

    uint32 state = 123456789u;
    int64 checksum = 0;
    for (int32 iteration = 0; iteration < 20000000; ++iteration) {
        state = state * 1664525u + 1013904223u;
        const int32 selected = static_cast<int32>(state & mask);
        Box* node = heapStorage[selected];
        const int64 next = (node->value + selected + (iteration & 1023)) & 2147483647LL;
        node->value = next;
        checksum = (checksum + next) & 2147483647LL;
    }

    for (int32 index = 0; index < size; ++index) delete heapStorage[index];
    return checksum;
}

int32 arenaOrder[1048576];
int32 arenaNext[1048576];
int64 arenaValues[1048576];

int64 arenaGraph() {
    constexpr int32 size = 1048576;
    constexpr int32 mask = size - 1;
    for (int32 index = 0; index < size; ++index) {
        arenaOrder[index] = index;
        arenaValues[index] = index * 17 + 3;
    }

    uint32 state = 123456789u;
    for (int32 index = size - 1; index > 0; --index) {
        state = state * 1103515245u + 12345u;
        const int32 selected = static_cast<int32>(state & 2147483647u) % (index + 1);
        const int32 temporary = arenaOrder[index];
        arenaOrder[index] = arenaOrder[selected];
        arenaOrder[selected] = temporary;
    }
    for (int32 index = 0; index < size; ++index)
        arenaNext[arenaOrder[index]] = arenaOrder[(index + 1) & mask];

    int32* nextPointer = arenaNext;
    int64* valuePointer = arenaValues;
    int32 current = arenaOrder[0];
    int64 checksum = 0;
    for (int32 iteration = 0; iteration < 5000000; ++iteration) {
        current = *(nextPointer + current);
        const int64 value = *(valuePointer + current);
        checksum = (checksum + value + iteration) & 2147483647LL;
    }
    return checksum;
}

class Node {
public:
    virtual ~Node() = default;
    virtual int64 evaluate(int32 round) const = 0;
};

class Leaf final : public Node {
    int64 value;
public:
    explicit Leaf(uint32 seed) : value(seed & 2147483647u) {}
    int64 evaluate(int32 round) const override {
        return (value + round) & 2147483647LL;
    }
};

class BinaryNode : public Node {
protected:
    Node* left;
    Node* right;
    int64 bias;
public:
    BinaryNode(Node* leftNode, Node* rightNode, uint32 seed)
        : left(leftNode), right(rightNode), bias((seed >> 8u) & 1023u) {}
    ~BinaryNode() override {
        delete left;
        delete right;
    }
};

class AddNode final : public BinaryNode {
public:
    using BinaryNode::BinaryNode;
    int64 evaluate(int32 round) const override {
        return (left->evaluate(round) * 3 + right->evaluate(round) * 5 + bias + round) & 2147483647LL;
    }
};

class XorNode final : public BinaryNode {
public:
    using BinaryNode::BinaryNode;
    int64 evaluate(int32 round) const override {
        return (left->evaluate(round) ^ (right->evaluate(round) * 9 + bias + round)) & 2147483647LL;
    }
};

Node* buildTree(int32 depth, uint32 seed) {
    if (depth == 0) return new Leaf(seed);
    const uint32 leftSeed = seed * 1664525u + 1013904223u;
    const uint32 rightSeed = leftSeed * 22695477u + 1u;
    Node* left = buildTree(depth - 1, leftSeed);
    Node* right = buildTree(depth - 1, rightSeed);
    if (((seed ^ static_cast<uint32>(depth)) & 1u) == 0)
        return new AddNode(left, right, seed);
    return new XorNode(left, right, seed);
}

int64 objectTree() {
    Node* root = buildTree(16, 123456789u);
    int64 checksum = 0;
    for (int32 round = 0; round < 200; ++round)
        checksum = (checksum + root->evaluate(round)) & 2147483647LL;
    delete root;
    return checksum;
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (equals(argv[1], "managed-deref")) printf("%lld\n", managedDeref());
    else if (equals(argv[1], "heap-nodes")) printf("%lld\n", heapNodes());
    else if (equals(argv[1], "arena-graph")) printf("%lld\n", arenaGraph());
    else if (equals(argv[1], "object-tree")) printf("%lld\n", objectTree());
    else return 2;
    return 0;
}
