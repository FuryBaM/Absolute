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

## Requirements

A generic class or struct may state what it requires of a parameter, written in
front of the name, where every other qualifier in this language is written:

```absolute
class VectorBuilder<copyable T> { ... }
struct Labelled<K, copyable V> { ... }
```

There is one requirement, and it names the single question the ownership model
already answers: `copyable` -- whether a second name for a value is an ordinary
thing to have. A number, a string, a `sub` borrow and a `weak` observer all
answer yes; a `T*` owner and an aggregate holding one answer no.

It is checked where the type is *used*, not inside the body, and that is the
point of it. A body that cannot serve an argument otherwise says so in every
line that touches it, each naming a private field the program never wrote, and
none of them on the line that asked. An instantiation a requirement refuses
does not report its body at all.

## Current boundaries

- generic parameters are types and do not yet support default values;
- `copyable` is the only requirement; general constraints/traits (`where`),
  variance, and partial specialization are not implemented yet;
- requirements are stated by classes and structs; an interface or a generic
  function that writes one is a syntax error rather than a qualifier that is
  read and then ignored;
- independently generic class/struct methods are rejected for now; methods may
  use the parameters of their containing generic type;
- operations in an open generic body must already be valid without assuming a
  numeric or other future trait constraint.

These boundaries keep the current model statically typed and allocation-free;
constraints can be layered on top without changing specialization ABI.
