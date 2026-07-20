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
- Array parameters always borrow. A callee must not release their owner field.
- Returning an owned variable or any of its aliases transfers the root owner to
  the caller. Cleanup in the callee skips that owner.
- A caller releases an owning array at scope exit. A fresh owning argument is
  released after the call, and an ignored owning result is released immediately.
- `copy(freshOwningArray())` releases the temporary source after the `memcpy`
  and transfers only the newly copied buffer.
- `free(null)` is valid, so a function may return either an owning copy or a
  borrowed global view on different paths without changing its ABI.

There is no separate `borrow` pointer kind or runtime marker. Managed pointer
parameters and array parameters are non-owning by language rule, slices are
zero-copy views, and `raw` remains the explicit unsafe escape hatch.

## Current boundary

Ownership of array-valued object fields, aggregate destruction, plugin-defined
resources, async captures, and lifetime diagnostics for temporary borrows still
need a unified design. Those items remain tracked in `TODO.md`; this document
covers local array descriptors, parameters, calls, slices, and returns.
