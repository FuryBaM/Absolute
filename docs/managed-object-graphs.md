# Managed object graphs

Absolute uses deterministic unique ownership for managed object graphs. It does
not use reference counting, tracing garbage collection, or a runtime cycle
collector.

## Graph model

- A strong managed field `T*` is an ownership edge. It accepts only a fresh
  owner returned by `new` or an owning function, or `null`.
- Every managed allocation therefore has at most one incoming ownership edge.
  Strong edges form an ownership forest rooted in local, returned, or aggregate
  owners rather than an arbitrary cyclic graph.
- A `weak T*` field is an observer edge. It may point to an ancestor, sibling,
  or unrelated managed object and never participates in cleanup.
- A cycle containing only observer edges is harmless: weak handles do not keep
  allocations alive and expire when their owners destroy the pointees.

The analyzer rejects assigning an existing subscriber to a strong field. An
obvious back-edge such as `root.child = root` or
`root.child.child = root` is diagnosed as `E_MANAGED_OWNERSHIP_CYCLE` with a
request to use `weak T*`. Other attempts to share one owner between strong
fields remain `E_RESOURCE_FIELD_REQUIRES_OWNER` errors.

```absolute
class Node {
    public Node* child;
    public weak Node* parent;
}

Node* root = new Node();
root.child = new Node();
root.child.parent = root;
```

## Cleanup

Destroying a root recursively invokes the generated destructor of each strong
child and invalidates every released generation-checked handle. Weak fields are
skipped. Existing weak observers consequently become false/null-like, including
after a runtime slot is reused for a later allocation.

This gives deterministic cleanup without a cycle collector:

1. generated destructors follow strong fields from leaves toward the root;
2. each strong handle is released exactly once;
3. weak back-edges are left untouched because they own nothing;
4. generation checks prevent those old weak handles from observing reused
   storage.

Use strong fields for containment (`child`, `payload`, owned components) and
weak fields for parents, peers, observers, caches, and graph cross-links. A
`raw T*` can bypass this model for native interop, but it has neither automatic
cleanup nor managed lifetime checks.

The executable graph test in `tests/managed-object-graphs.abs` covers nested
strong children, weak parent links, recursive destruction, observer expiry,
slot reuse, and the runtime leak checker. Its LLVM check also proves that the
generated `GraphNode` destructor releases the strong child exactly once and
does not release the weak parent.
