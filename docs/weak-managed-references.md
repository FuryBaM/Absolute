# Weak managed references

Absolute supports explicit non-owning managed references with `weak T*`.
They use the same generation-checked 64-bit handle as ordinary managed
pointers, but never own or destroy the referenced allocation.

```absolute
class Node {
    public weak Node* parent;
}

Node* root = new Node();
Node* child = new Node();
child.parent = root;

weak Node* observer = root;
delete root;
assert(!observer);
assert(!child.parent);
```

No weak-specific runtime allocation, reference count, control block, or pointer
kind is introduced. A weak value stores the existing `(slot, generation)`
handle. Truth tests call the normal managed validity operation, and dereference
uses the normal checked managed lookup. Reusing a slot cannot revive an old weak
reference because its saved generation no longer matches.

## Ownership rules

- A strong managed pointer `T*` converts implicitly to `weak T*` when the
  pointee types are compatible.
- A weak pointer can be copied, passed to synchronous functions, returned, set
  to `null`, compared, tested for validity, and safely cast while remaining weak.
- `weak T*` never converts back to `T*`. Absolute currently has no `lock` or
  promotion operation because the runtime is ownership-based rather than
  reference-counted.
- `new T(...)` cannot initialize a weak destination directly. The fresh
  allocation must first be bound to a strong owner, otherwise nobody could
  release it.
- `delete` and `move(...)` reject weak pointers because they own no resource.
- Returning a weak handle to a function-local owner is rejected because the
  result would already be expired when observed by the caller.
- Weak pointers are not permitted in the C ABI or async task context. Raw
  pointers remain the explicit native ABI representation; async pointer sharing
  needs a separate thread-safety design.

## Fields and cycles

Ordinary managed fields are owning resource slots and accept only a fresh owner
or `null`. Weak fields accept an existing managed owner/subscriber and are
skipped by generated aggregate cleanup. This makes parent links, observers,
caches, and graph back-edges expressible without creating an ownership cycle.

Local assignments such as `T* subscriber = owner` remain supported as inferred
non-owning subscribers for short-lived code. `weak T*` is the explicit,
storage-safe contract needed for fields, APIs, and returned observer handles.
