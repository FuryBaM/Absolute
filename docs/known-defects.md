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

None verified. The one candidate carried over from the previous session did not
survive a check, and the correction is recorded below rather than dropped,
because the wrong version of it was written down first.

### Retired: "a module-scope struct never runs its destroy()"

It cannot be declared at all. Every form is refused:

```absolute
struct Handle { int32 value; void destroy() { value = 0; } }
Handle GLOBAL = Handle();      // Top-level executable statements cannot be
                               // combined with an explicit main function
```

Module-scope declarations only accept constant primitive initializers, which is
what `GlobalConstant` in `Absolute-CodeGen/src/codegen_module.cpp` supports. A
struct needs a constructor call, so the declaration reads as an executable
statement and is rejected. Primitives and primitive arrays work; struct-typed
globals do not exist, so there is no destructor to schedule and nothing silently
wrong.

Two things are still worth doing here, neither of them a defect:

- **The message is misleading.** "Top-level executable statements cannot be
  combined with an explicit main function" describes the parse, not the
  problem. Something like "a module-scope initializer must be a constant"
  would point at the actual limit.
- **The limitation is undocumented.** Nothing states that module scope is
  primitives only.

## 2. Missing features that fail loudly

These produce honest syntax errors. They are not silent, and nothing computes
a wrong result — but they are ordinary notation a user will expect.

| Form | Status |
|---|---|
| `0xFF`, `0b1010`, `0o17` | not lexed |
| `1_000_000` | not lexed |
| `1e3`, `1e-9` | not lexed |
| `>>=`, `<<=` | not parsed |

The exponent form is the most conspicuous: a language with `double` and no way
to write `1e-9` is missing something people reach for immediately.

## 3. Behaviour that contradicts the documentation

A chain through a managed field unwinds root-first (trace `123`), while
`docs/resource-ownership.md` describes reverse field order. Fields *within* an
object do unwind in reverse (trace `321`), so only the chain case disagrees.

`tests/ownership-torture-graph.abs` pins the part that is not in dispute —
every level is released exactly once — and accepts any permutation, so the test
will not block either resolution. Someone has to decide whether the document or
the traversal is wrong.

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
  `tests/global-variables.abs`.
- **Ownership, scheduler, collections, CI matrix.** Findings here came slowly
  and needed real effort; coverage is dense.

## 5. Not swept — candidates, in rough order of expected yield

Untested guesses, offered as starting points rather than predictions:

1. **Pointer arithmetic** — `raw T*` offsets, comparison, subtraction, and the
   interaction with array storage.
2. **Generic instantiation** — specialisation with mixed widths and
   signedness, given that signedness was where every recent defect lived.
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
