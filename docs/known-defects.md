# Known defects and where to look next

Written as a handoff. Everything here is either reproduced or explicitly
marked as unverified, and the sections are separated by kind on purpose: a
missing feature that fails loudly is not the same problem as a wrong answer
that no one notices.

Reproducers assume a built compiler:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DLLVM_DIR="$(llvm-config-18 --cmakedir)"
cmake --build build --parallel 4
build/Release/absolutec file.abs --build-exe -o app && ./app
```

## 1. Open defects

One unreproduced observation, below. Everything else this section held was
closed in the session that follows the one which wrote it; each entry records
what the fix was, so a regression is recognizable rather than rediscovered.

### Unreproduced: `absolute.run-task-scheduling` aborted once under load

Seen once in eight full parallel runs of the suite on Linux. It did not
reproduce: 30 direct runs of the binary and 12 scheduler-only parallel ctest
runs were all clean, and four further full runs were clean. No diagnostic was
captured, so there is nothing here but the fact that it happened.

Recorded rather than dropped, because a scheduler abort that appears only under
CPU contention is exactly the kind of thing a green suite hides. The next
occurrence should be captured with `--output-on-failure` before anything else
is concluded.

### Fixed: a base class's destroy() was silently skipped

Found while checking section 3, and the only silent wrong answer in this file.
A derived `destroy()` shadows its base's under the same method key in
`ClassInfo::methods`, and the synthesized destructor took that one entry, so a
base that closes a handle or frees native memory stopped doing it the moment
any derived class declared a hook of its own. Its *fields* were still released,
which is why nothing failed loudly -- only the hand-written half went missing.

```absolute
class Base    { void destroy() { print(4); } }
class Derived : Base { void destroy() { print(5); } }
// delete a Derived: printed 5, never 4
```

`EmitClassDestructor` now walks the base chain through `declaredMethods`, which
holds only what each class declares itself, and calls every hook most-derived
first, deduplicated by link name so an inherited entry is not called twice.
Pinned by `tests/destruction-order.abs`, including the dispatch through a
base-class pointer.

### Retired: "a module-scope struct never runs its destroy()"

It cannot be declared at all. Every form is refused:

```absolute
struct Handle { int32 value; void destroy() { value = 0; } }
Handle GLOBAL;                 // a module-scope declaration must be a
                               // primitive with a constant initializer
```

Module-scope declarations only accept constant primitive initializers, which is
what `GlobalConstant` in `Absolute-CodeGen/src/codegen_module.cpp` supports.
Primitives and primitive arrays work; struct-typed globals do not exist, so
there is no destructor to schedule and nothing silently wrong.

The two follow-ups this entry left behind are done:

- **The message.** It used to read "Top-level executable statements cannot be
  combined with an explicit main function", which describes the parse and not
  the problem -- the declaration is not executable. It now names the limit, the
  variable and its type, with a file, line and column.
- **The limitation.** `docs/module-scope.md` states what module scope accepts,
  what it refuses and why, and where the rest belongs.

## 2. Missing features that fail loudly

All four now lex. They were honest syntax errors rather than wrong answers, but
they are ordinary notation, and a language with `double` and no way to write
`1e-9` was missing something people reach for immediately.

| Form | Status |
|---|---|
| `0xFF`, `0b1010`, `0o17` | lexed, any case, with `_` between digits |
| `1_000_000` | lexed |
| `1e3`, `1e-9`, `1.5e2` | lexed |
| `>>=`, `<<=` | parsed |

Bases and separators are normalized in the lexer, because every consumer
downstream reads the token text with `std::stoull`, which would have read
`0xFF` as 0 and stopped at the `x`. The exponent cannot be normalized away, so
float detection moved from "the text contains a dot" to `IsFloatingLiteral`,
which also knows the `e` in `0xE1` is a digit. A literal the pattern can only
match part of -- `0xZZ`, `1_`, `1e`, `12abc` -- is a lexical error at its own
line and column instead of a number followed by a stray identifier.

Covered by `tests/literal-notation.abs` and `tests/literal-notation-errors.abs`.

## 3. Behaviour that contradicts the documentation

Resolved in favour of the traversal: the document was incomplete, not wrong.

A chain through a managed field unwinds root-first (trace `123`) and fields
within an object unwind in reverse declaration order (trace `321`). These are
halves of one rule -- the object's own `destroy()` runs first, then its fields
in reverse order -- so an owner is torn down before what it owns and a hook can
still reach its own fields, while later fields go before the earlier ones they
may depend on. That is the same order C++ gives a destructor body and its
members, and reversing the chain would mean a hook running after its own fields
were gone.

`docs/resource-ownership.md` stated only the field half. It now states both,
and `tests/destruction-order.abs` pins them.

## 4. Swept and clean — do not redo these

Recording the negative results, because they cost time to obtain and they
narrow where to look.

- **Strings and text: 23 of 23 clean.** Bytes against code points, Cyrillic,
  emoji, `split` with consecutive and trailing separators, `indexOf` on
  multibyte input, case conversion, `replace` producing an empty result,
  comparison.
- **Numeric semantics: swept and now covered.** 10 of 24 probes failed; all
  fixed, with tests at the boundaries. See `tests/unsigned-arithmetic.abs`,
  `tests/wide-integer-literals.abs`, `tests/division-edges.abs`,
  `tests/global-variables.abs`, `tests/literal-notation.abs`.
- **Ownership, scheduler, collections, CI matrix.** Findings here came slowly
  and needed real effort; coverage is dense.

## 5. Not swept — candidates, in rough order of expected yield

Untested guesses, offered as starting points rather than predictions:

1. ~~**Pointer arithmetic**~~ — swept. The arithmetic itself was right:
   offsets scale by the element (checked with `int64` and with a struct whose
   stride includes padding, where adding bytes would show), differences count
   elements and keep their sign, comparisons order addresses, stores through a
   computed address land in the array behind it, and both a managed pointee and
   a `void` pointee are refused. What was missing was the notation a buffer
   walk is written in: `p += n`, `p++` and `p[i]` were all refused, the first
   because a compound assignment checked the step against the target type, so
   `p += 2` read as assigning an int32 to a pointer while `p = p + 2` was
   accepted. All three now lower through one shared offset. An indexer declared
   on the pointee still wins over `p[i]`, which is what `raw IndexedBox*` in
   `tests/indexers-codegen.abs` relies on. See `tests/pointer-arithmetic.abs`
   and `tests/pointer-arithmetic-errors.abs`.
2. ~~**Generic instantiation**~~ — swept. Substitution held: two
   specializations with the same machine width and different signedness do not
   share a body, a field or a return of substituted type keeps its signedness,
   and comparisons inside an open generic body pick the right instruction.
   Inference did not: every integer literal was typed int32 by the analyzer
   regardless of magnitude, so `identity(10000000000)` inferred `T = int32` and
   returned 1410065408. A literal now takes the narrowest type that holds it,
   which is what the backend always did when emitting one -- the two only
   agreed while the literal fit. The same disagreement was truncating
   `int32 x = 4294967296` to 0 in silence; a literal that cannot fit its target
   is now `E_LITERAL_OUT_OF_RANGE`, while narrowing a *value* stays allowed.
   Arithmetic on a bare `T` is refused for want of constraints, which is a
   documented boundary rather than a defect (docs/generics.md). See
   `tests/generic-instantiation-widths.abs` and `tests/literal-range-errors.abs`.
3. **The WASM backend against native** — the differential corpus now covers
   float and integer edges, but only in shapes it generates.
4. **Collection boundaries** — empty, single-element, capacity transitions,
   iterator invalidation under mutation.

## 6. The method that worked

Worth repeating, because reading code did not find any of this.

Write small programs whose correct answer is unambiguous, run them, compare
against the expected value. Probe **boundaries**, not typical values.

The trap to avoid: a probe can pass for the wrong reason. The first unsigned
test divided `4294967295` by 2 and asserted `2147483647` — whose top bit is
clear, so a signed widening looked identical and a second defect hid behind the
passing test. Choose values where the wrong behaviour must show: above the
signed maximum of the width, at `2^31`, at `2^32`, at the type's minimum.

And when a fix lands in one path, ask which other callers reach the same code
by another route. The compound-assignment defect was found that way, not by
sweeping again: `a = a / b` had been fixed while `a /= b` still used an untyped
overload.

## 7. Environment note

During this work the container repeatedly reverted the working tree to an older
commit and deleted the build directory. Pushed commits were never affected, but
a stale build silently produced misleading results once — probe values read as
zero because the compiler predated a fix, which looked like a defect and was
not.

Check `git log --oneline -1` before trusting a probe, and rebuild after any
unexplained result.
