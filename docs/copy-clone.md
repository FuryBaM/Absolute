# Explicit copying and cloning

Absolute does not use C++ copy constructors or the rule of five. Copying,
ownership transfer, and cleanup are separate operations:

- ordinary resource-free `struct` values use implicit field-wise value copies;
- `move(value)` transfers an owning value and invalidates the source binding;
- `destroy()` is the optional user lifecycle hook invoked by generated cleanup;
- `copy(arrayOrSlice)` allocates independent array storage;
- `copy(value)` invokes the value's explicit `clone()` contract.

## The `clone()` contract

A cloneable struct declares a public, zero-argument, const method returning the
same struct type:

```absolute
struct Image {
    int32[] pixels;

    public Image clone() const {
        Image result;
        result.pixels = copy(pixels[]);
        return move(result);
    }
}

Image independent = copy(source);
```

A cloneable class or interface returns a new strong managed pointer of the same
static pointee type:

```absolute
class Shape {
    public virtual Shape* clone() const {
        return new Shape();
    }
}

Shape* duplicate = copy(original);
```

Virtual `clone()` methods use the normal vtable dispatch. Consequently, copying
a base or interface pointer can preserve the dynamic implementation while the
result keeps the declared static pointer type.

The compiler rejects `copy(value)` when:

- the source is not an array, slice, class, interface, or struct;
- a non-array source is not a stable lvalue;
- the type has no instance `clone()` with zero parameters;
- `clone()` is not public or not const;
- a struct clone returns a different struct type;
- a class or interface clone does not return the matching strong managed
  pointer type.

An owning clone result must be consumed by an initializer, assignment, owning
field/parameter, or return. Managed pointer parameters are borrows, so a fresh
managed clone must first be stored in an owner variable. The source remains
valid and unchanged.

## Shallow and deep copies

Ordinary assignment of a resource-free struct remains a field-wise value copy.
Raw pointers and borrowed descriptors inside such a value copy their address or
view and never clone the pointee.

Deep or logical copying is always visible:

```absolute
Pair b = a;              // ordinary value copy
int32[] ys = copy(xs);   // independent array storage
Tree* other = copy(tree); // user-defined clone()
```

This keeps potentially expensive allocation out of plain assignment and avoids
implicit duplication of unique owners.
