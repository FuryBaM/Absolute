# Aggregate resource ownership

Absolute treats managed pointers and owning array descriptors stored in object
fields as resources of the containing aggregate. This is a compile-time role;
no `keep` or `borrow` marker is stored in the object at runtime.

## Field rules

- A managed `T*` field owns the fresh handle assigned by `new` or an owning
  function result. Assigning an arbitrary subscriber is rejected.
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
