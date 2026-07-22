# Value-type ABI

Absolute `struct` values always have value semantics. Assignment, parameter
passing, field initialization, and return produce an independent aggregate
value. Pointer and array-descriptor fields copy their stored handle or view;
copying a struct does not recursively clone pointee or array storage.

## Calling convention

The portable Absolute-to-Absolute threshold is 16 bytes, including target
layout padding:

- structs of 16 bytes or less are LLVM parameters and results directly;
- structs larger than 16 bytes are indirect parameters and results;
- primitive, enum, pointer, string, task, and array-descriptor ABI lowering is
  unchanged.

An indirect result changes the LLVM return type to `void` and inserts a hidden
`ptr %__result` as the first parameter. For an instance method, `this` follows
the result pointer. Explicit parameters retain their source order.

An indirect by-value parameter is a pointer to an isolated temporary allocated
by the caller. The caller stores the evaluated source value into that temporary
before the call. The callee binds its parameter directly to this private
storage, so parameter mutation cannot modify the caller's source value.

For example, source signatures equivalent to:

```absolute
Large makeLarge(int64 seed);
Large Box.read();
void consume(Large value);
```

are lowered conceptually as:

```llvm
define void @makeLarge(ptr %__result, i64 %seed)
define void @Box.read(ptr %__result, ptr %this)
define void @consume(ptr %value)
```

## Copy and move rules

The explicit `move(...)` operation transfers a resource-owning aggregate and
invalidates its source binding. Resource-free values retain normal copy
semantics. A compiler may forward return storage, remove aggregate load/store
pairs, or reuse a temporary only under the normal as-if rule: the optimization
must not introduce aliasing and must not change observable pointer, cleanup, or
mutation behavior.

Parameter-only `T&`/`const T&` borrows are designed separately from ownership
and are not part of the implemented ABI yet. Their proposed lowering, escape
rules, and benchmark evidence are documented in
[`value-references.md`](value-references.md).

All objects linked through this internal ABI must be rebuilt with the same
compiler ABI revision. `extern "C"` declarations and `export "C"` definitions
are intentionally excluded: their aggregate layout and passing rules are
defined by the selected platform C ABI, so a native wrapper is required when
its struct convention differs.
