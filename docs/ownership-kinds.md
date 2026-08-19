# Ownership as part of the type

## The problem this exists to solve

A handle says where it points. Until now it did not say, strongly enough, what
relation the value has to the lifetime of what it points at. `T*` meant both
"the owner" and "a copy of the owner that must not release anything", and
nothing in the type told them apart — the difference lived in the analyzer's
flow analysis, per symbol, and evaporated the moment a value was stored in a
container, substituted into a generic, or returned.

Two open defects are the same defect seen from two sides:

- **An array never releases what its elements own.** Giving the array that
  ownership breaks `Vector<T*>` in three places, because `toArray` must hand
  back non-owning copies and the type it returns is the same `T*` that would
  own. Recorded in `docs/known-defects.md` §15, including the attempt that was
  made and withdrawn.
- **A string has no lifetime**, so every string a program builds is lost. §1.

Fixing them separately would produce two special rules, and the next container —
a map, a closure environment, a tuple — would find a third way onto the same
mine.

## The model

Ownership is a property of the type, spelled by the qualifier the language
already has:

| Written | Kind | Copyable | Releases |
|---|---|---|---|
| `T*` | Unique | no — use `move` | yes, destroys the object |
| `shared T*` | Shared | yes | yes, the last one destroys |
| `sub T*` | Sub | yes | no — its owner does |
| `weak T*` | Weak | yes | no — it holds a generation |
| `raw T*` | Raw | yes | no — unmanaged |

The rule the whole thing rests on:

> Ownership belongs to the value, but the role it is allowed to play is
> encoded in the type. A container does not change the semantics of its
> element.

`T*` stays an owner wherever it lands — a field, an array, `Vector<T*>`,
`Pair<int32, T*>` — and `sub T*` never quietly becomes one.

## Why containers must not ask "is this managed?"

```cpp
if (isManaged(elementType)) destroy(element);   // wrong
```

`T*`, `sub T*` and `weak T*` are all managed handles and all have different
lifetime semantics. The question a container asks is not what a value points at
but what destroying it means:

```cpp
struct TypeSemantics { bool copyable; bool movable; bool needsDrop; DropKind dropKind; };
```

Then `Array<T>` knows nothing about pointers. It drops each element that needs
dropping, and `Array<string>`, `Array<Vector<T*>>` and a struct holding both
work for the same reason, without any of them being special-cased.

## Where strings fit

A string stops being an exception. Its storage becomes a shared owner:

```
string        ->  shared StringStorage*
StringStorage ->  reference count, capacity, characters
```

`string b = a;` stays an ordinary copy — making it a move would be unpleasant in
a general-purpose language — and the count decides when the storage goes. That
is one type using shared ownership, not the memory model becoming reference
counted.

## Order of work

1. **Ownership kind in the canonical type.** *(done)* One definition in
   `Absolute-Parser/include/type_names.h`, used by the analyzer and the backend
   instead of each keeping its own prefix tests.
2. **No qualifier is lost.** *(done for substitution and unification)*
   Both substitution paths spelled out `raw` and `weak` by hand and returned
   everything else unqualified, so `shared T*` became `T*` on the way through a
   generic; unification compared raw-ness and weak-ness separately, so the two
   also unified with each other. Pinned by
   `tests/ownership-qualifier-generics.abs`.
3. **Copy / move / drop become type-driven** rather than pointer-driven: one
   `TypeSemantics` per type, built recursively from its parts.
4. **`sub T*` becomes spellable**, and copying a `T*` without `move` is refused.
   This is the step that changes existing programs.
5. **Arrays drop their elements** through `TypeSemantics`, which is §15 closed.
6. **`Vector<T>` follows** — the same generic code must produce a different drop
   for `Vector<T*>` than for `Vector<sub T*>`. That is the check that says the
   model is really in place.
7. **`StringStorage` and shared ownership**, then `format` returning an ordinary
   string, which is §1 closed.

## What proves each step

Step 6 is the one that matters: if `Vector<T*>` and `Vector<sub T*>` go through
the same generic body and come out with different drops, the distinction is
genuinely carried by the type rather than reconstructed by a special case.

For step 7, the test is a plateau: a loop building a string per iteration must
reach a stable resident size instead of growing without bound, which is what
two million `format` calls reaching 64.7 MB currently does.

Nested shapes to keep honest at every step:

```
T*[]            sub T*[]        string[]        T*[][]
Vector<T*>      Vector<sub T*>  struct { string name; Node* node; }
```
