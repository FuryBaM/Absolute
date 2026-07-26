# Isolation instead of borrow checking

Absolute does not use a general lexical borrow checker to make collection
iteration and task concurrency safe. The standard representations remove the
invalid states instead.

## Collection iteration

A standard collection iterator owns an immutable snapshot of the collection's
backing storage. The current `Vector`, `Map`, and `Set` implementation establishes
this semantic baseline by eagerly copying the visible elements in `iterate()`.
Consequently an iterator may be stored or returned, and mutating the original
collection while iterating is well-defined: the iterator continues over the
snapshot captured by `iterate()`. A `foreach` statement owns and destroys its
managed iterator on normal and non-local exits.

The planned storage optimization shares immutable backing versions and makes a
structural mutation publish a new version using copy-on-write. It must never edit
or free a version retained by an iterator. This changes allocation cost, not the
observable snapshot semantics or their safety guarantee.

The safe collection API does not expose mutable element or storage addresses.
Operations that must structurally mutate during traversal use a transient builder
or cursor forked from an immutable snapshot. Only `finish()` freezes and publishes
the new collection; every older alias remains valid and continues to see its old
version. Moving a uniquely held version may let the runtime reuse storage, but
that is only an optimization and never the safety condition. Algorithms such as
`retain` and `drain` use the same transient representation. Generation counters
are not the safety model; an unsafe FFI/raw adapter must copy storage or explicitly
leave safe code.

## Task isolation

Each task executes in its own object/handle domain. The spawn, await, and channel
ABI carries a closed message envelope rather than arbitrary captured addresses.
Normal managed, weak, or raw pointer bit patterns have no valid message-envelope
representation, so ordinary mutable objects cannot be shared accidentally.

Only two cross-domain representations exist:

1. an immutable message value/blob, copied or backed by read-only storage;
2. a sealed transfer capsule containing a unique object graph.

A capsule is constructed only by the explicit `seal(move(owner))` operation,
not by casting a pointer or integer. Sealing rotates the root managed handle
generation, so ordinary aliases retained by the sender expire at runtime.
`std.concurrent.TransferChannel<T>` transports the opaque one-shot capsule and
`unseal<T>` establishes the receiver's new owner. A failed send restores the
capsule to the caller; destroying a channel also destroys queued capsules.
One-shot task results use the same ownership direction.

Shared mutable facilities are separate opaque capabilities such as atomics,
mutex cells, semaphores, actors, and services. Their internal value cannot be
extracted as an ordinary pointer/reference, and all access is mediated by their
synchronized API. A lock guard is domain-local and cannot be encoded in an await
continuation or channel message.

Plugin resources are task-local by default. A plugin must explicitly implement
immutable-message copying or atomic detach/rehome hooks before its resource can
cross an isolate boundary.
