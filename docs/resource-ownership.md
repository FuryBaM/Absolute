# Aggregate resource ownership

Absolute treats managed pointers and owning array descriptors stored in object
fields as resources of the containing aggregate. This is a compile-time role;
no `keep` or `borrow` marker is stored in the object at runtime.

## Field rules

- A managed `T*` field owns the fresh handle assigned by `new` or an owning
  function result. Assigning an arbitrary subscriber is rejected.
- Reading a managed field borrows it from the containing aggregate. Returning
  that field as if it were a transferable owner is rejected, including from a
  method invoked on a temporary aggregate.
- An array field owns a non-null descriptor owner produced by `copy(...)` or an
  owning function result. A global array descriptor is safe because its owner is
  null. A local array or slice cannot escape into a field.
- A `raw T*` field is never cleaned automatically. It is an explicitly unsafe
  address; whichever code owns its allocation must still call `delete`.
- Reassigning a resource field destroys its previous value before storing the
  new owner. Constructors rely on zero-initialized object storage, so the same
  rule is safe for first assignment.
- Auto-property backing fields follow the same cleanup rule in their generated
  setter.

## Aggregate destruction

The compiler synthesizes one internal destructor for every class or struct that
owns resources, directly or through nested fields. It runs in this order:

1. The user `destroy()` hook of the object itself, if it declares one.
2. The object's fields, in reverse declaration/layout order.

An owner is therefore torn down before what it owns, which is what lets
`destroy()` still read and use its own fields. Applied recursively down a chain
of owning fields, the outermost owner's hook runs first and the innermost last:
for `root -> mid -> leaf`, the order is `root.destroy()`, `mid.destroy()`,
`leaf.destroy()`. Within one object the direction is the opposite, because
later fields may depend on earlier ones: a class declaring `a`, `b`, `c`
releases `c`, then `b`, then `a`. `tests/destruction-order.abs` pins both.

Managed pointees are destroyed recursively, then their generation-checked
handles are invalidated; array owners are passed to `free` and their
descriptors are zeroed.

Vtable slot zero is reserved for the class destructor. Deleting or automatically
releasing a class through a base-class or interface managed/raw pointer loads
that slot from the dynamic object, so the most-derived fields are destroyed.
Struct cleanup is a direct call because structs have no dynamic dispatch.

## Object graphs and cycles

Strong managed fields form a unique-ownership forest: every field accepts only
a fresh owner or `null`, so an allocation cannot acquire a second strong parent.
The analyzer rejects explicit strong back-edges with
`E_MANAGED_OWNERSHIP_CYCLE`. Parent, peer, observer, and other cross-links use
`weak T*`; generated destructors skip those fields, and their generation-checked
handles expire when the strong owner recursively destroys the graph.

This strategy needs no reference counting, tracing GC, or runtime cycle
collector. See [managed-object-graphs.md](managed-object-graphs.md) for the graph
model, diagnostics, and cleanup contract.

## Managed pointer moves

`move(owner)` transfers a strong managed handle to a variable, owning field, or
return value. The destination becomes the unique owner, the source storage is
zeroed and becomes compile-time moved-from, and existing subscriber/weak
lifetime tracking follows the destination. A moved-from binding cannot be read
or deleted until it is initialized with a fresh owner.

Moving subscribers, weak or const references, invalid pointers, and discarded
move results are rejected. An ordinary resource parameter receives a view when
called with an lvalue and ownership when called with `move(owner)` or a fresh
owner. The callee cleans up an owner-role parameter unless it is moved onward.
An unguarded move/delete/owning return makes the parameter owner-required;
`isOwner(parameter)` allows one body to handle both roles.
See
[managed-pointer-move.md](managed-pointer-move.md) for the exact contract.

## Value semantics boundary

A struct or class value that recursively contains an owning resource cannot be
bitwise copied safely. The analyzer rejects ordinary aggregate assignment and
unsafe ownership escapes. A resource parameter is a non-owning view by default;
explicit `move(...)` transfers the resource and clears the source. An explicit `copy(value)` is
available only when the type supplies a public zero-argument `clone() const`
method that creates an independent owner. Resource-free structs keep the
existing value ABI and copy semantics. See
[`copy-clone.md`](copy-clone.md).

Static managed, array, and aggregate fields remain unsupported. Plugin-defined
resources also remain pending until the versioned plugin API can register their
destroy/move behavior and participate in analyzer escape checks and CodeGen
cleanup.
