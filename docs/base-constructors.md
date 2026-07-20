# Base constructors

Class constructors initialize the inheritance chain from the direct base class
to the most-derived class. A derived constructor can pass arguments to its
direct base constructor with a contextual `base(...)` call:

```absolute
class ValueBase {
    public int32 value;

    public ValueBase(int32 initial) {
        value = initial;
    }
}

class AdjustedValue : ValueBase {
    public AdjustedValue(int32 initial) {
        base(initial + 1);
        value += 1;
    }
}
```

`base(...)` must be the first statement in a class constructor. It is consumed
as constructor metadata rather than emitted as an ordinary function call. It
is rejected in functions, struct constructors, classes without a base class,
and later positions in a constructor body.

If a derived constructor does not contain an explicit base call, the compiler
inserts `base()` before its body. This is valid only when the direct base
constructor takes no arguments. A required-argument base constructor produces a
semantic error until the derived constructor calls it explicitly with the
correct argument count and types.

When a derived class declares no constructor, the compiler synthesizes a
zero-argument constructor if any class in its base chain needs construction.
This makes the automatic chain recursive:

```absolute
class Root {
    public Root() {
        println("root");
    }
}

class Middle : Root {
}

class Leaf : Middle {
}
```

Constructing `Leaf` calls `Root()`, then the synthetic `Middle()` body, and
finally the synthetic `Leaf()` body. Base-constructor failures use the normal
exception propagation path, so the derived body does not run after a failure.

Absolute currently supports one constructor declaration per class. Constructor
overloads and named base-constructor selection remain future language work.
