# Static fields and methods

Classes, structs, and interfaces may declare static fields and methods with the
`static` modifier. Static members are accessed through their declaring type (or
a derived type), not through an object:

```absolute
class Counter {
    public static int64 value = 40;
    public const static int64 step = 2;

    public static int64 advance() {
        value += step;
        return value;
    }
}

assert(Counter.advance() == 42);
```

Each static field is emitted as one LLVM global and is excluded from the
instance layout. A static method has no hidden `this` parameter. It may access
static fields and invoke other static methods by either their short name or a
qualified `Type.member` name. Static members inherited from a base class refer
to the same storage and function as the base declaration.

Interface static members follow the same storage and call ABI. They must be
public, do not occupy an instance vtable slot, and are not contracts that an
implementing class must repeat. A child interface can access an inherited
static field or method, and the field still refers to the storage owned by its
declaring interface. Static interface methods must have bodies; static abstract
dispatch is deferred until generic constraints can name and validate it.

The analyzer rejects instance-field access from a static method, instance
members accessed through a type, static members accessed through an object,
and `virtual`, `override`, `sealed`, or trailing `const` on static methods.

Static field initialization currently supports compile-time number, boolean,
character, string, null, and negated-number literals. The supported storage
types are primitives, enums, strings, and raw pointers. Managed pointers,
arrays, aggregate values, runtime initializers, and static members of generic
types require a later module-initialization and ownership design and are
rejected for now.
