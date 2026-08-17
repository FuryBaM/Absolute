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
parameter: `identity(10000000000)` instantiates with `int64`. A minus in front
of the literal is part of the constant, so `-9223372036854775808` is an `int64`
and not the negation of a `uint64`.

## Floating point to integer

A conversion from `float` or `double` to an integer type truncates toward zero,
and saturates when the value does not fit: anything past an end clamps to that
end, and `NaN` becomes zero. The same rule holds on every target, so a native
build and a wasm build give the same answer.

```absolute
double huge = 1e18;
int32 clamped = huge as int32;      // 2147483647
int32 negative = -1e18 as int32;    // -2147483648
int32 fromNan = (0.0 / 0.0) as int32;  // 0
uint32 fromNegative = -1.0 as uint32;  // 0
```

Saturation is a deliberate choice over the C rule, where a value that does not
fit is undefined: that produced a different number on each build, and a value
that broke the formatter it was passed to.

`as` binds like a suffix, tighter than a prefix `-`, so `-1.0 as uint32` is the
negation of a converted `1` and not the conversion of `-1`. The two readings
agree everywhere except an unsigned target, where one saturates to zero and the
other wraps to the maximum; parenthesise when the distinction matters.

```absolute
uint32 negated = -1.0 as uint32;     // 4294967295: -(1.0 as uint32)
uint32 converted = (-1.0) as uint32; // 0: the negative value saturates
```

## Floating point literals

A literal is a `double`. Subnormal values are accepted, with the precision a
double gives them; a literal that keeps nothing of what was written — one that
overflows to infinity, or underflows all the way to zero — is refused with
`E_LITERAL_OUT_OF_RANGE`.

```absolute
double small = 1e-308;   // accepted, subnormal
double lost = 1e400;     // refused: larger than a double can hold
```


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
