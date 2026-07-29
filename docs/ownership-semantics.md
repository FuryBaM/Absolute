# Ownership and argument semantics

Absolute separates the shape of an expression from the lifetime contract of a
parameter.

| Form | Meaning | May mutate caller | May outlive call | Cleanup owner |
| --- | --- | ---: | ---: | --- |
| `T value` | independent value | no | yes, as its own value | normal value rules |
| `T& place` / `ref T place` | exclusive place borrow | yes | no | caller |
| `const T& value` | shared value borrow | no | no | caller or call temporary |
| `T* ptr` | managed borrow (`ManagedSub`) | pointee only | no ownership transfer | caller |
| `consume T* ptr` | managed ownership transfer | pointee only | callee may move onward | callee |
| `raw T* ptr` | unchecked-address view | yes | programmer-controlled | explicit raw owner |
| `weak T* ptr` | generation-checked observer | pointee while live | no ownership | strong owner |

An lvalue is a **place**: it has identity and may be readable, writable, and
addressable. Literals, call results, copies, and `move(...)` results are
**values**. `&place` creates a raw address view; `*pointer` yields a place when
the pointer is valid.

`move(ownerPlace)` consumes an owning place and produces an owner value. The
source becomes moved-from until reinitialized. A fresh `new T(...)` is already
an owner value and can enter a consume parameter directly. Ordinary pointer
parameters borrow and reject both `move(owner)` and a fresh anonymous owner,
because neither has a destination responsible for cleanup.

Consume parameters are supported for strong managed pointers, owning arrays,
and resource-owning aggregates, including specialized generic `consume T*`
parameters. Raw and weak pointers are views. Value references are synchronous
aliases. These categories do not implicitly convert into one another.

The executable matrix is `tests/ownership-semantics-matrix.abs`; its negative
counterpart checks illegal combinations and transfers. The value/reference
benchmark is under `benchmarks/value-ref-suite`, while broader raw, managed,
heap-node, graph, and virtual-object comparisons are under
`benchmarks/pointer-object-suite`.
