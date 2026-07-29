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

A managed owner move may be received by:

- a new strong managed variable;
- assignment to an empty or existing owner variable;
- an owning strong field;
- a managed owner return;
- an ordinary `T*` parameter, with its hidden role set to owner.

Managed pointer parameters are role-polymorphic. Passing `owner` is a
synchronous borrow. Passing `move(owner)` transfers the owner role; the source
becomes moved-from at the call and the callee destroys the resource on exit
unless it moves the parameter onward.

```absolute
void close(Node* node) {
    // Using move(node) makes this function owner-required.
}

close(move(owner));  // owner place becomes moved-from
close(new Node());   // a fresh owner value transfers directly
```

No ownership keyword is part of the callable signature. The analyzer infers an
owner requirement from an unguarded `move`, `delete`, owning store, or owning
return of the parameter. A function that intentionally handles either role can
branch on `isOwner(node)`. The same ABI role applies to owning arrays and
resource-owning structs. Raw and weak pointers are always views.

For a task or channel boundary, `seal(move(owner))` is the explicit transferring
operation. It rotates the managed handle generation immediately, invalidating
all aliases left in the sender, and returns an opaque capsule handle.
`unseal<T>(capsule)` consumes that handle and creates the new `T*` owner.
Application code normally uses these through `std.concurrent.Transfer<T>` and
`std.concurrent.TransferChannel<T>`.

The analyzer also rejects moving a subscriber, weak pointer, const source,
invalid pointer, or discarding a move result. None of these rules adds a runtime
pointer kind: managed moves reuse the existing 64-bit `(slot, generation)`
handle and its normal validity checks.
