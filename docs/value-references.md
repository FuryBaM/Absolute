# Value references for large structs

## Decision

Absolute supports parameter-only `const ref T` and `ref T` borrows for large,
resource-free value types. They are an ABI and aliasing feature, not a new
runtime pointer kind and not an alternative ownership model.

The first implementation is intentionally narrow:

- `T` must be a concrete value type for which `TypeOwnsResources(T)` is false;
- the useful default threshold is the existing indirect-ABI boundary, greater
  than 16 bytes;
- reference parameters are permitted on functions, methods, and constructors;
- they are forbidden as return types, fields, globals, array elements, generic
  arguments, lambda captures, task payloads, and stored function values;
- reference mode is part of override, interface, and mangled-signature identity;
- overloads cannot differ only by value/reference mode because calls have no
  separate argument-side `ref` marker.

This keeps the feature separate from managed pointers. A value reference has no
allocation, generation, ownership, null state, pointer arithmetic, or `delete`.
Its lifetime is exactly one synchronous call.

## Source and ABI rules

`const ref T` accepts an initialized value and grants shared read-only access.
It may also accept a temporary whose lifetime is extended through the call.
`ref T` accepts only an initialized mutable lvalue and grants exclusive mutable
access for the call. Neither form may be rebound by the callee.

Both forms lower to one non-null pointer to caller-owned storage:

```absolute
int64 inspect(const ref WideValue value);
void normalize(ref WideValue value);
```

```llvm
define i64 @inspect(ptr nonnull nocapture readonly %value)
define void @normalize(ptr nonnull nocapture %value)
```

The caller does not create the isolated aggregate copy required by the current
large by-value ABI. `const ref` may be reborrowed as `const ref`; `ref` may be
reborrowed as either mode. Conversion to raw or managed pointers is rejected.

At one call site, an lvalue passed as `ref` must not overlap any other argument
that can read or write the same storage. Multiple `const ref` arguments may
alias. Initially, overlap should be diagnosed conservatively by root symbol;
field-sensitive disjointness can be added later.

All escapes are compile-time errors: converting the borrow to `raw`, capturing
it in a closure, scheduling it with `spawn`, or otherwise retaining it beyond
the call is invalid. The grammar admits reference mode only on parameters, and
async/C-ABI callables, lambdas, stored function values, resource-owning structs,
classes, primitives, and generic aggregates reject it explicitly.

## Benchmark evidence

`benchmarks/value-ref-suite` compares the existing by-value ABI with a raw
pointer used strictly as a physical-ABI proxy for future `const ref`. The
workload passes a 128-byte resource-free struct to an `@noinline` read-only
function 20 million times. Both variants produce checksum `1518646656`.

On 2026-07-22, an AMD Ryzen 7 5700U Windows Release run with three warmups and
15 alternating samples produced:

| Mode | Median | Min | Max |
| --- | ---: | ---: | ---: |
| Current by-value ABI | 0.156551 s | 0.147269 s | 0.215253 s |
| Borrowed-address proxy | 0.131116 s | 0.126508 s | 0.192288 s |

The median speedup is 1.19x. This is large enough to justify explicit reference
parameters at opaque call boundaries, but not large enough to replace value
semantics implicitly. Small values remain by-value, and the programmer opts in
where ABI stability or measured copying cost matters.

The raw-pointer benchmark does not claim source-level safety equivalence. It
only measures the upper bound of removing the caller-side aggregate copy; the
rules above supply the missing null, mutation, ownership, and escape guarantees.
