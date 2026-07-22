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
owns resources, directly or through nested fields. Fields are destroyed in
reverse declaration/layout order. Managed pointees are destroyed recursively,
then their generation-checked handles are invalidated; array owners are passed
to `free` and their descriptors are zeroed.

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

Moving subscribers, weak or const references, invalid pointers, discarded move
results, and moves into ordinary borrowed managed parameters are rejected. See
[managed-pointer-move.md](managed-pointer-move.md) for the exact contract.

## Value semantics boundary

A struct or class value that recursively contains an owning resource cannot be
bitwise copied safely. For now the analyzer rejects aggregate assignment,
by-value arguments, and by-value returns for those types. A future explicit
`move` operation will transfer the resource and clear the source. Resource-free
structs keep the existing value ABI and copy semantics.

Static managed, array, and aggregate fields remain unsupported. Plugin-defined
resources also remain pending until the versioned plugin API can register their
destroy/move behavior and participate in analyzer escape checks and CodeGen
cleanup.
