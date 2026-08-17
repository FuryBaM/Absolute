# Implicit conversions

Absolute applies built-in implicit conversions in assignments, initializers,
function and method arguments, return expressions, array literals, ternary
expressions, comparisons, and overload resolution.

## Numeric values

Numeric values convert between `int8`, `int16`, `int32`, `int64`, `uint8`,
`uint16`, `uint32`, `uint64`, `char`, `float`, and `double`:

```absolute
int16 small = 42;
int64 wide = small;
double real = wide;
float compact = real;
```

Both widening and narrowing conversions are allowed for compatibility with
existing Absolute programs. Use `as` when a conversion should be visible at
the call site:

```absolute
float frameTime = timer.deltaTime() as float;
```

A literal is not covered by that allowance. Narrowing a *value* keeps whatever
the conversion leaves behind, but a literal is a constant the compiler can
measure, so one that cannot be represented in its target type is refused rather
than truncated:

```absolute
int64 wide = someInt64;
int32 narrowed = wide;      // allowed: a value conversion
int32 tooLarge = 4294967296;  // refused: E_LITERAL_OUT_OF_RANGE
uint32 mask = 4294967295;     // allowed: it fits uint32
int32 smallest = -2147483648; // allowed: the sign belongs to the literal
```

An integer literal takes the narrowest type that holds it — `int32`, else
`int64`, else `uint64` — which is also what decides an inferred generic
parameter: `identity(10000000000)` instantiates with `int64`.


## References and null

- `null` converts to pointer and C-function-pointer types.
- A derived pointer converts to a compatible base-class or interface pointer
  without changing its ownership mode.
- A strong managed pointer converts to a compatible `weak` pointer.
- A `weak` pointer does not implicitly convert back to a strong pointer.
- Raw and managed pointer modes never convert implicitly between each other.

```absolute
Derived* derived = new Derived();
Base* base = derived;
weak Base* observer = derived;
```

Conversions between unrelated values such as `string` to `int32` or `int32`
to `bool` are compile-time errors. Absolute does not currently support
user-declared conversion operators; wrap that conversion in a named function
or constructor so that the operation remains explicit.
