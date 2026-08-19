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
| `T*` | Unique | yes, as a subscriber | yes, destroys the object |
| `sub T*` | Sub | yes | no — its owner does |
| `weak T*` | Weak | yes | no — it holds a generation |
| `raw T*` | Raw | yes | no — unmanaged |

There is deliberately no shared kind. A second sort of owner buys nothing that
those four do not already say, and one was tried: `shared T*` was added in July
and walked back the same day, leaving a spelling the lexer reserved, the parser
accepted and the analyzer refused. It has now been removed outright rather than
left refused, so the word is an ordinary identifier again. Reference counting,
if some type needs it, belongs inside that type rather than in the handle
vocabulary.

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

A string stops being an exception. It becomes an ordinary value type whose
storage is counted:

```
string        ->  a value holding a handle to its storage
StringStorage ->  reference count, capacity, characters
```

`string b = a;` stays an ordinary copy — making it a move would be unpleasant in
a general-purpose language — and the count decides when the storage goes. The
counting lives inside `string`, not in the handle vocabulary: the type system
does not need a shared kind to express it, and adding one would put a second
sort of owner in front of every reader of every type.

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
3. **Copy / move / drop become type-driven** rather than pointer-driven.
   *(done)* One `TypeSemantics` per type -- `copyable`, `movable`, `needsDrop`
   and a `DropKind` -- built recursively from the type's parts, answered by
   `Analyzer::SemanticsOfType` and `CodeGenerator::Impl::SemanticsOfTypeName`
   from their own type tables. Releasing a value is now a switch on the drop
   kind; it used to be a chain of questions about the shape of the type name
   ("is it a closure, is it a strong managed pointer, is it an array..."), and
   each of those was a second place that had to agree with `TypeNeedsCleanup`
   about the answer. `copyable` and `movable` are computed but not yet
   enforced -- that is step 4, and it is the step that changes programs.
4. **`sub T*` becomes spellable.** *(done)* It is there to let the distinction
   be written down where it matters -- a container element, a field, a generic
   argument -- **not** to forbid the short form.

   `T* b = a;` stays legal and keeps taking a subscriber, and `isOwner()`
   answers which of the two a value is at runtime. Refusing it was tried and
   reverted: it is a convenience worth keeping, and the model does not need it
   gone. What the model needs is that where the answer must survive being
   stored, passed or substituted, it can be *said* -- and now it can.

   `sub T*` now works as a local, a field, a parameter, a return type and
   through a generic, and a subscriber cannot be assigned back to an owner --
   the arrow runs one way. Nothing existing changed: 688 green. Getting there
   replaced the three independent flags on the pointer AST node with the single
   ownership kind, which immediately turned up two more places that had been
   dropping a qualifier -- cloning a property's type and cloning an indexer's
   type both kept raw-ness and weak-ness and let the rest fall off -- and one
   more that spelled out two prefixes when building a backend type name. Each
   lookahead that had to step over a qualifier also had its own list, which is
   why `sub` worked for a local before it worked for a parameter; they share
   one step now.

   Two things were measured before starting it, and both change how it should
   be done:

   - **`sub` cannot be an ordinary keyword.** It is already used as an
     identifier -- `int32 sub = 3;` in `tests/ownership-semantics-matrix.abs`,
     which asserts in so many words that "consume, owner, and sub are ordinary
     identifiers", and a local named `sub` in `std/form.abs`. It has to be
     contextual: a qualifier only where a type is expected and the next token
     begins a type. The rule that makes it unambiguous is that the qualifier
     only means anything in front of a pointer.
   - **The blast radius of the refusal is 35 sites, all in `tests/`.** The
     standard library never binds an owner to another owner-typed variable;
     the tests do it deliberately, because subscriber semantics is what they
     are testing. So the refusal is affordable in one change, and the sites
     that must be rewritten are the ones that document the behaviour being
     formalised.

   The audit to redo after any change here, since it decides which existing
   checks mean "is a handle" and which mean "is an owner":
   `IsStrongManagedPointerType` is currently both, and with `Sub` in the model
   those two readings come apart.
5. **The rules run inside a generic body.** *(done)* A generic body is analyzed
   once, with its parameters standing for themselves, and never again against
   any instantiation -- so inside `Container<T>` every ownership rule asked
   whether `T` is a pointer, got no, and said nothing. That is not one missing
   check, it is all of them, and it is exactly where a container of owners
   lives.

   The body is still analyzed once. Where a rule could not run, the pass now
   records what it saw -- the shape, the type as written, the location -- and
   each instantiation substitutes and asks the rule then. `Sieve<Node*>`,
   `Sieve<sub Node*>` and `Sieve<int32>` get three different answers on the
   same line of the same body. Recorded in full in `docs/known-defects.md`
   §16; pinned by `tests/generic-body-ownership-errors.abs` and
   `tests/generic-body-ownership.abs`.

   Without this, step 7 could not be more than half true: the drop would
   differ while the rules that govern either side did not run at all.
6. **Arrays drop their elements** through `TypeSemantics`, which is §15 closed.

   With step 4 as decided, this is where the remaining design question lands:
   `sub T*[]` plainly does not own its elements, and `T*[]` is the case that
   still has two readings. Whatever `T*[]` is defined to mean, it has to be one
   thing -- that is what the withdrawn attempt got wrong by leaving it to be
   inferred per element.

   **The decision: `T*[]` owns its elements, `sub T*[]` does not, and the
   array never asks.** It is the element type that answers, the same way it
   answers for a field or a local. This follows from the rule the whole model
   rests on -- the container does not change the semantics of the element --
   and it is the only reading under which `Vector<Node*>` and
   `Vector<sub Node*>` can share a body and still be right.

   What it costs is not in the array. It is in every place that currently
   treats a slot as a value that can be read twice, and `std/collections/vector.abs`
   has one of each -- which is why step 7 is a separate step rather than a
   consequence:

   - **Growth copies the handles and then drops the source.** `push` does
     `unsafeArrayCopy(newItems, items, _count)` and `items = move(newItems)`;
     the old array still holds every handle that was just memcpy'd, so dropping
     it releases every element the vector still uses. Either the copy is a move
     that clears the source, or growth releases the old storage without
     dropping through it.
   - **`pop` hands out a slot it does not clear.** `_count` is decremented and
     `unsafeArrayGet` reads the handle; the array's length is its capacity, not
     `_count`, so the array still drops what was handed to the caller. This is
     exactly what the withdrawn attempt turned into a silent premature destroy.
     It wants a take -- read and null in one step -- not a get.
   - **`clear` sets `_count = 0`** and releases nothing, which leaks every
     element for as long as the vector lives.
   - **`removeAt` shifts with `items[i] = items[i + 1]`**, which under this
     reading writes an owner over an owner and leaves the last slot holding a
     duplicate. It wants per-slot moves and a cleared tail.
   - **`toArray` copies every handle into a second array**, which is two arrays
     owning the same objects -- already refused for `copy` by
     `E_COPY_OWNING_ELEMENTS`, and the same refusal has to reach here.

   Two things already in place are what make this affordable: array storage is
   zero-initialized (`tests/array-zero-initialization.abs`), so a null slot is
   a slot that owns nothing and drop can skip it, and the rules now run inside
   a generic body (step 5), so `Vector<T>`'s own code is checked against each
   `T` rather than never.
7. **`Vector<T>` follows** — the same generic code must produce a different drop
   for `Vector<T*>` than for `Vector<sub T*>`. That is the check that says the
   model is really in place.
8. **`StringStorage` and shared ownership**, then `format` returning an ordinary
   string, which is §1 closed.

## What proves each step

Step 7 is the one that matters: if `Vector<T*>` and `Vector<sub T*>` go through
the same generic body and come out with different drops, the distinction is
genuinely carried by the type rather than reconstructed by a special case.

For step 8, the test is a plateau: a loop building a string per iteration must
reach a stable resident size instead of growing without bound, which is what
two million `format` calls reaching 64.7 MB currently does.

Nested shapes to keep honest at every step:

```
T*[]            sub T*[]        string[]        T*[][]
Vector<T*>      Vector<sub T*>  struct { string name; Node* node; }
```
