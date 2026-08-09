# Tuples and `params`

Absolute has structural tuple values and safe variadic calls.

## Tuples

A tuple type is written as `tuple<T0,T1,...>`. A tuple literal uses
parentheses and at least two values:

```absolute
tuple<int32,string> pair = (7, "seven");
pair.item0 = 8;
println(pair.item1);
```

Elements are addressed with zero-based members `item0`, `item1`, and so on.
`length` and `count` are compile-time-sized, read-only `int32` properties.
Tuples can be stored, passed to functions, and returned from functions. Large
tuples use the same indirect value ABI as large structs.

Tuple elements currently must not own runtime resources. Managed owners,
arrays, and resource-owning aggregates should be placed in a named struct
until tuple cleanup and move semantics are implemented.

## Variadic parameters

The final parameter of an Absolute function or method may use `params`:

```absolute
int32 sum(int32 initial, params int32[] args) {
    int32 result = initial;
    foreach (int32 value in args) {
        result += value;
    }
    return result;
}

sum(10);
sum(10, 1, 2, 3);

int32[] values = {4, 5, 6};
sum(10, values);
```

Inside the function, `args` is an ordinary one-dimensional array view. At an
expanded call, the compiler packs trailing values into caller-side stack
storage and passes a borrowed descriptor; no heap allocation or hidden copy is
performed. Passing an existing array of the exact parameter type passes its
descriptor directly.

A `params` parameter:

- must be last;
- must have a one-dimensional array type;
- cannot be `ref`, `const`, or have a default value;
- cannot appear on `extern`, `export`, or `async` functions;
- currently requires a resource-free element type.

Normal overloads win over an equally good `params` expansion.

Command-line arguments are separate from variadic parameters and remain
available through `std.env.args()`.
