# Type aliases and const values

## Type aliases

`using` declares a transparent alias for an existing Absolute type:

```absolute
using Count = int32;
using IntBox = Box<int32>;
using Buffer = int32[];
```

Aliases may be declared at module or namespace scope. Their target may be a
primitive, enum, struct, class, concrete generic specialization, array, task,
or raw/managed pointer type. Alias declarations are order-independent and may
refer to another alias. Cycles, duplicate names, and unknown targets are
semantic errors. Generic alias parameters are not supported yet; alias a
concrete generic specialization instead.

Aliases do not create a distinct runtime type or LLVM layout. Overload
resolution, generic inference, ABI checks, and code generation use the fully
resolved underlying type.

## Const values

Variables, fields, and parameters use a prefix `const`:

```absolute
const int32 answer = 42;

int32 add(const int32 left, int32 right) {
    return left + right;
}
```

A local or global const variable requires an initializer. A const field may be
initialized by its containing constructor, but cannot be assigned afterward.
The analyzer rejects direct and compound assignment, increment/decrement,
deletion, and taking a mutable raw address from a const value. Array elements
and fields of a const value object are also immutable.

Const is shallow for pointer bindings: `const T* value` prevents replacing or
deleting the pointer variable, but does not make the pointed-to `T` const.
Pointee constness will be a separate type-system feature if needed.

Methods place `const` after the parameter list:

```absolute
int32 read() const {
    return value;
}
```

A const method cannot mutate fields or invoke a non-const method. Interface
implementations must match the const contract of the declared method.
