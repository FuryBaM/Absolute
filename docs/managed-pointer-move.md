# Managed pointer ownership transfer

`move(owner)` transfers a strong managed `T*` owner without allocating,
destroying, or copying its pointee.

```absolute
Node* a = new Node();
Node* subscriber = a;
weak Node* observer = a;

Node* b = move(a);
delete b;

assert(!subscriber);
assert(!observer);
```

The generated code loads the existing generation-checked handle, writes a zero
handle into the source binding, and stores the loaded handle in the destination.
The destination becomes the unique owner. Existing subscribers and weak
observers still refer to the same runtime handle; compile-time lifetime tracking
is rebound to the destination so deleting it expires those aliases.

## Moved-from source

After `Node* b = move(a)`, `a` is moved-from rather than an ordinary readable
`null`. Its storage is zero at runtime, but the analyzer marks the binding
uninitialized. Reading, dereferencing, deleting, or moving `a` again is rejected
until it receives a fresh owner:

```absolute
Node* b = move(a);
a = new Node(); // valid reinitialization
```

This prevents accidental dependence on a moved-from value while keeping the
binding reusable.

## Valid destinations

A managed owner move may be consumed by:

- a new strong managed variable;
- assignment to an empty or existing owner variable;
- an owning strong field;
- a managed owner return.

Ordinary managed pointer parameters are borrowed in Absolute. Passing
`move(owner)` to such a parameter is rejected because the callee has no owning
parameter role and would otherwise lose the allocation. Pass `owner` without
`move(...)` for a synchronous borrow.

For a task or channel boundary, `seal(move(owner))` is the explicit consuming
operation. It rotates the managed handle generation immediately, invalidating
all aliases left in the sender, and returns an opaque capsule handle.
`unseal<T>(capsule)` consumes that handle and creates the new `T*` owner.
Application code normally uses these through `std.concurrent.Transfer<T>` and
`std.concurrent.TransferChannel<T>`.

The analyzer also rejects moving a subscriber, weak pointer, const source,
invalid pointer, or discarding a move result. None of these rules adds a runtime
pointer kind: managed moves reuse the existing 64-bit `(slot, generation)`
handle and its normal validity checks.
