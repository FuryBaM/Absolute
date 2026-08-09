# Array ownership and slices

Absolute uses one array descriptor for sized-array views, slices, copied arrays,
parameters, and return values. The internal descriptor contains:

1. `data` — the first element visible through the current view;
2. `owner` — the allocation base returned by `malloc`, or null for borrowed and
   statically allocated storage;
3. one `int64` length for every array dimension.

Keeping `data` and `owner` separate is required for slices. For example,
`copy(values)[1:3]` advances `data`, but `owner` still identifies the beginning
of the allocation and is the only pointer passed to `free`.

## Ownership rules

- Sized local arrays and array literals use stack storage. Their descriptors
  have a null owner.
- Global arrays use static storage and also have a null owner.
- `copy(arrayOrSlice)` allocates a new buffer and returns an owning descriptor.
- A local variable initialized from a fresh `copy(...)` or an array-returning
  call owns the descriptor's non-null allocation.
- Creating another view or slice does not duplicate ownership. The new variable
  is a borrower and retains the root owner's symbol for return analysis.
- An array lvalue passed normally is a borrowed view. `move(ownerArray)` or a
  fresh owning result passes the owner role, which the callee releases on exit
  unless moved onward.
- Returning an owned variable or any of its aliases transfers the root owner to
  the caller. Cleanup in the callee skips that owner.
- A caller releases an owning array at scope exit. A fresh owning argument is
  released by the owner-role callee, and an ignored owning result is released
  immediately.
- `copy(freshOwningArray())` releases the temporary source after the `memcpy`
  and transfers only the newly copied buffer.
- `free(null)` is valid, so a function may return either an owning copy or a
  borrowed global view on different paths without changing its ABI.

There is no separate `borrow` source type. Internal Absolute calls carry a
hidden ownership-role bit for resource parameters; slices remain zero-copy
views, and `raw` remains the explicit unsafe escape hatch.

## Audited unchecked access

`unsafeArrayGet(array, index)` and `unsafeArraySet(array, index, value)` are
one-dimensional, unchecked compiler intrinsics. They perform no length check
and an invalid index is undefined behavior. Safe application code should use
ordinary `array[index]`.

`unsafeArrayData(array)` returns a `raw T*` to the first element. The pointer
borrows the array storage, must not be deleted, and is invalidated when its
owner is destroyed or replaces the backing allocation. `Vector.unsafeData()`
exposes the same explicitly unsafe view; callers must not retain it across
`push` or another structural mutation.

The intrinsics exist for standard-library containers that already validate a
logical index against their own element count. For example, `Vector` checks
`0 <= index < count` and then uses the intrinsic to avoid repeating a second
capacity-array bounds check. The public collection operation remains checked;
only its already-proven internal access is unchecked.

## Aggregate fields

An array-valued class or struct field is an owning resource slot. Store a fresh
`copy(...)`, transfer a local owning descriptor with `move(...)`, store an
owning function result, or use a global borrowed descriptor. A local array or
slice cannot otherwise escape into a field because its backing storage may
disappear before the aggregate. Reassignment frees the old non-null owner, and
the aggregate destructor frees the final value.

The analyzer rejects local or borrowed views that escape through a return or an
aggregate field unless an explicit `copy(...)`, returned owner, or global view
makes the backing storage long-lived. Async task payloads cannot contain arrays
or slices because the current task context stores only lifetime-independent
scalar and enum values. Plugin-defined resource boundaries remain future work;
see `docs/resource-ownership.md` and `TODO.md`.
