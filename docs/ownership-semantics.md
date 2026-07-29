# Ownership and argument semantics

Absolute separates a parameter's type from the ownership role of each call.
There are no `owner`, `sub`, or `consume` parameter modifiers.

| Form | Meaning | May mutate caller | Lifetime / cleanup |
| --- | --- | ---: | --- |
| `T value` | independent resource-free value | no | normal value rules |
| `T& place` / `ref T place` | exclusive place borrow | yes | caller |
| `const T& value` | shared value borrow | no | caller or call temporary |
| `T* ptr` called with `ptr` | managed view | pointee only | caller retains ownership |
| `T* ptr` called with `move(ptr)` | managed owner | pointee only | callee RAII |
| `raw T* ptr` | unchecked-address view | yes | explicit raw owner |
| `weak T* ptr` | generation-checked observer | pointee while live | strong owner |

The same role-polymorphic rule applies to owning arrays and resource-owning
aggregates. The internal Absolute ABI carries one hidden ownership bit beside
each such parameter. It is not a source-language type or keyword.

An lvalue is a **place**: it has identity and may be readable, writable, and
addressable. Literals, call results, copies, and `move(...)` results are
**values**. `&place` creates a raw address view; `*pointer` yields a place when
the pointer is valid.

`move(ownerPlace)` transfers the owner value and immediately leaves the source
moved-from. It does not turn a subscriber or weak observer into an owner.
A fresh owning result, such as `new T(...)` or `copy(array)`, also enters a
resource parameter with the owner role.

Inside a role-polymorphic function, `isOwner(parameter)` reads the incoming
role. An owner-role parameter is destroyed on every exit unless it is moved or
returned onward. A view-role parameter is never destroyed by the callee.

If a function unconditionally moves, deletes, stores, or returns a parameter as
an owner, the analyzer infers that this parameter requires ownership. Direct
calls must then use `move(owner)` or a fresh owner value. Code that accepts both
roles guards the ownership path:

```absolute
void process(Node* node) {
    if (isOwner(node)) {
        Node* saved = move(node);
        // ...
    }
    else {
        // borrowed view
    }
}
```

Raw and weak pointers remain explicit pointer representations because they
change safety and cleanup behavior; they are not ownership roles.

The executable matrix is `tests/ownership-semantics-matrix.abs`; its negative
counterpart checks invalid owner-required calls and subscriber/weak moves.
Benchmarks live under `benchmarks/value-ref-suite` and
`benchmarks/pointer-object-suite`.
