# Generics

Absolute generics are compile-time templates implemented through
monomorphization. Only specializations used by the program are emitted to LLVM,
so `identity<int32>` and `identity<double>` have independent native signatures
and bodies without runtime boxing or type tags.

## Generic functions

Generic parameters follow the function name. Calls may provide them explicitly
or infer them structurally from arguments:

```absolute
T identity<T>(T value) {
    return value;
}

T first<T>(T[] values) {
    return values[0];
}

int32 number = identity(42);
double decimal = identity<double>(21.5);
```

Inference understands direct parameters, pointers, arrays, and nested generic
types. Every generic parameter must be resolved, repeated occurrences must
resolve to the same type, and explicit argument counts must match.

## Generic structs and classes

Both value layouts and methods are specialized using the concrete arguments:

```absolute
struct Pair<T> {
    public T first;
    public T second;
}

class Box<T> {
    public T value;

    public Box(T initialValue) {
        value = initialValue;
    }

    public T read() {
        return value;
    }
}

Box<int32>* value = new Box<int32>(42);
```

`Box<int32>` and `Box<double>` produce distinct LLVM structure layouts,
constructors, and method symbols. Generic types cannot be used without type
arguments.

## Current boundaries

- generic parameters are types and do not yet support default values;
- constraints/traits (`where`), variance, and partial specialization are not
  implemented yet;
- independently generic class/struct methods are rejected for now; methods may
  use the parameters of their containing generic type;
- operations in an open generic body must already be valid without assuming a
  numeric or other future trait constraint.

These boundaries keep the current model statically typed and allocation-free;
constraints can be layered on top without changing specialization ABI.
