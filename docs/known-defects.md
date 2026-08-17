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

Nothing open. Every entry this section held has been closed, and each one
records what the fix was, so a regression is recognizable rather than
rediscovered.

### Fixed: an owner produced inside a larger expression was lost

The ownership rules only ever named the top of a statement, so an owner
produced deeper in one was created, read, and dropped:

```absolute
if (make() != null) { println("compared"); }   // leaked
int32 v = make().value;                        // leaked
```

Each ended with "memory leak detected for handle N" and an abort at exit.
`std.json.parse(text)` used in a condition is the shape that found it. The two
answers on the table were to refuse such an expression or to release it; the
release was chosen, so `if (parse(x) != null)` keeps working.

An unbound owner is now destroyed at the end of the statement that produced it
-- the end of the statement, not the point of use, so `make().child.value` can
still read through the object after the first borrow. The accounting is the
part that had to be right, and it is narrow on purpose: only an expression that
*produces* an owner is registered, which is why a variable holding one is never
touched, and an owner handed to a function stays the callee's. Releasing means
the same two steps `delete` takes -- what the object owns, then the object --
because destroying the handle alone left every managed field behind.

Three things it had to get right beyond the leak, each found by a probe rather
than by reading:

- **A borrow on a path that may not run** -- the right side of `&&`, a ternary
  arm, a loop condition -- is released on that path. A release after the merge
  would name a value that path never produced, which is invalid IR, not a leak.
- **A body emitted inside another body.** A lambda is emitted in the middle of
  the function that writes it, and an SSA value belongs to exactly one
  function; the first version put the lambda's destroys in `main`, and the
  verifier caught it. Every body emitter now isolates its own temporaries, and
  anything left pending when a body ends fails the build instead of leaking.
- **Leaving through an exception**, where the statement's own release never
  runs. The unwind path releases them.

Pinned by `tests/temporary-owners.abs`, which counts destructions rather than
watching for a crash: a missed release shows up as a leak at exit, a double
release as a count that is too high. `docs/ownership-semantics.md` states the
rule.

### Fixed: `absolute.run-task-scheduling` aborted under load

Recorded here as an unreproduced observation -- seen once in eight full
parallel runs, then clean through 30 direct runs, 12 scheduler-only parallel
runs and four more full runs. It reappeared once during this session's suite
runs, which was the second sighting, and this time it reproduced: with eight
busy loops competing for the CPU, the binary failed **18 times in 60**. Idle,
it never fails. The doc's own advice applied -- a scheduler failure that only
appears under contention is exactly what a green suite hides -- and the missing
piece was simply making the machine busy while running it.

The failure was always the same assertion, `metrics completed tasks`, and the
cause is an ordering one rather than a race on the counter itself. A worker
finishing a task marked it done and woke its waiters under the task's lock, and
incremented `completedTasks` *after* releasing that lock. `await` returns the
moment it sees `done`, so a program could await two tasks and then read a
metric that did not include either of them yet. The counter is now incremented
inside the same critical section, before the waiters are told -- anything that
can observe the completion observes the count that goes with it.

Same 60 runs under the same contention afterwards: no failures.

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

All four have now been swept; what each turned up is recorded in place. Kept as
a list rather than deleted, because the negative results are the useful part:
they say where not to look again.

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
3. ~~**The WASM backend against native**~~ — swept by hand, outside the shapes
   the generator produces: every runnable test in `tests/` built for both
   backends and diffed, output and exit status. Arithmetic, ownership, strings,
   collections, exceptions and the runtime diagnostics all agree -- a bounds
   failure prints the same message and exits the same way on both. Two link
   gaps and one behavioural difference came out of it. The optimizer rewrites
   `printf("%c", value)` into `putchar`, which the wasm runtime shim did not
   define, so printing a char failed to link for wasm while building natively;
   `getchar` was missing for the same reason on the reading side. Both are now
   in the shim, reading as end-of-input because the sandbox has no stdin. The
   difference that remains is by construction: a wasm instance cannot wait on
   another worker, so channels there never block -- a send to a full channel
   fails and a receive from an empty one yields zero, which a program cannot
   tell from a real zero. `tests/concurrency-stress.abs` loses messages on wasm
   for exactly that reason. Now written down in docs/wasm-target.md, with
   `absolute_channel_receive_checked` as the way to tell the two apart. See
   `tests/wasm-console-libcalls.abs` and `tests/wasm-stdin-eof.abs`.
4. ~~**Collection boundaries**~~ — swept. Vector, Deque, Map, Set, HashMap and
   PriorityQueue all hold at their edges: empty and single-element containers,
   reallocation points, a deque whose contents straddle the end of its buffer
   before it grows, hash-map tombstones left by removing every other key and
   then refilled, and mutation under a live iterator, which yields its snapshot
   as documented. Everything passed on the first run; `tests/collection-
   boundaries.abs` keeps the record so a boundary nobody checks cannot drift.
   The one defect the sweep found was not in the containers. Reading a field of
   the `KeyValuePair` a map iterator returns did not compile: a property getter
   and a function return a copy, not storage, and member access asked for an
   address anyway, so `pair.key` and `config().timeout` failed with "a property
   is not addressable" or "expression is not assignable". The value is now
   copied into a temporary and read from there, and a *write* through such a
   copy is refused by the analyzer, where the message can carry a file and a
   line, instead of by the backend naming its own mechanism. See
   `tests/value-place-access.abs` and `tests/value-place-errors.abs`.

## 6. Beyond the list: floating point

Swept after section 5 ran out, because it is where the correct answer is least
obvious and a wrong one least visible. Three defects, all found by comparing
against what IEEE 754 says rather than against what looked reasonable:

- **`nan != nan` was false.** The backend emitted the ordered comparison for
  `!=`, which is false whenever an operand is NaN, so `==` and `!=` both
  answered false and `a != b` stopped being the negation of `a == b`.
- **`1e-308` did not compile.** `std::stod` throws on every result the C
  library flags as out of range, subnormals included, and the exception reached
  the user as a bare `Error: stod` -- the same shape the integer path had
  already been fixed for. Subnormals are values a double holds; they are
  accepted now, and only a literal that overflows to infinity or underflows to
  zero is refused.
- **`1e18 as int32` was poison.** The plain conversions are undefined when the
  value does not fit: a different number on each build, `inf as int32` breaking
  the formatter it was passed to, and native and wasm disagreeing on every
  out-of-range case. Conversions saturate now, which is defined on every
  target.

One thing to know rather than to fix: `as` binds like a suffix, tighter than a
prefix minus, so `-1.0 as uint32` is the negation of a converted `1`. The
readings differ only for unsigned targets; both are pinned in
`tests/floating-point-edges.abs`.

## 7. Beyond the list: a library that aborted, and an assert nobody could read

Probing the standard library with input chosen to break it -- indices past the
end, negative counts, malformed encodings, a directory where a file belongs --
turned up three things, two of them fatal to the process.

- **A legal JSON number aborted the program**, and so did a malformed
  `\uXXXX` escape. Section 8 has the detail.
- **`std.fs.readText` on a directory aborted.** A directory opens as a stream
  and fails on the first read, and libstdc++ reports that by throwing
  `std::ios_base::failure` out of the iterator -- past the runtime, past
  generated code that has no handler, into `std::terminate`. It is an ordinary
  filesystem error now, named as one.
- **A failing `assert` printed nothing.** The message went to stdout and
  `abort()` does not flush stdio, so whenever stdout was not a terminal -- a
  pipe, a file, a CI log -- the text died in the buffer and the failure was a
  bare exit code 134. Every test in this repository is written on `assert`, so
  every one of them failed mutely; the suite leans on `PASS_REGULAR_EXPRESSION`
  markers partly because of this. Generated code flushes before aborting now.

Everything else refused its input properly, and those refusals became readable
for the first time once the unhandled report learned to print messages. The
sweep is pinned by `tests/hostile-input.abs`.

## 8. Beyond the list: JSON input that killed the process

The floating-point sweep pointed at a second `std::stod`, this one in the JSON
parser, and the consequence there is worse than a compile error.

- **A legal number aborted the program.** `std::stod` throws whenever the C
  library flags the result out of range, subnormals included, and the parser
  did not catch it: a document containing `1e-308` killed the process with an
  uncaught `std::out_of_range` from inside `std.json.parse`. Any program
  parsing input it did not write could be stopped by a number in the payload.
- **A malformed `\uXXXX` escape did the same.** `std::stoul("ZZZZ", …, 16)`
  throws `invalid_argument`, and the parser already had an error channel it
  never got to use.

Both are input, not programming errors, so both now travel through that error
channel or through the value the C library returns: an overflowing number
becomes infinity, an underflowing one becomes zero, a bad escape returns null
with a message. `std.json.getDoubleOr` was added while writing the test, since
the typed getters could read an integer, a string and a bool but not a double.
Covered by `tests/std-json-edges.abs`.

## 9. Beyond the list: a shift nobody could predict

Swept after the switch and match diagnostics, on the reasoning that a shift has
the same shape as the division that was already found wrong: an operand range
LLVM leaves undefined, in an operator that looks total.

- **A shift by the width, or more, was undefined.** `1 << 32` on an int32
  printed -1780665664, `1 << 40` produced a value `format` could not print at
  all, `int64 1 << 64` printed 9, and each rebuild was free to choose
  differently. Not a wrap and not a zero -- poison, in the same way an
  unchecked `sdiv` by zero is.
- **A negative amount was the same case**, since the shift instruction reads
  the amount as unsigned.
- **The compound forms reached the same instruction**, so `c <<= 32` was wrong
  in exactly the same way.

Refused rather than defined, following the answer division overflow got: a
program that shifts a 32-bit value by 32 has a bug in it, and a plausible
looking 0 hides it. An amount written in the source is refused by the analyzer,
where the message carries a file, a line and the width it was measured against;
a computed amount is checked where it is used and the program exits with a
message. Native and wasm agree on both paths.

The width a shift happens at is the width of its *result* type -- the wider of
the two operands, like every other binary operator here -- so `one << count`
with an int64 count is a 64-bit shift even though `one` is an int32. That was
already true; it is now written down (docs/implicit-conversions.md) and pinned,
because it decides which amounts are legal. Everything around the shifts came
out clean: complement at each width, mixed-signedness `&`, `|`, `^`, and the
signed/unsigned split in `>>`. See `tests/shift-boundaries.abs`,
`tests/shift-range-errors.abs` and `tests/shift-amount-runtime.abs`.

## 10. Beyond the list: an enum that could not carry a number

Found by probing enums for wrong answers and finding none -- everything an enum
could not do, it refused loudly. But one of those refusals was `enum Status {
NotFound = 404 }`, a syntax error, and an enum crosses the C boundary as an i32
(docs/native-c-abi.md) whose number is exactly what the other side reads. A
member was numbered by position and nothing else, so an enum could not stand
for an HTTP status, an errno, or any C constant, and the number was not
readable from Absolute either.

Members can be given numbers now, a member without one continues from its
predecessor, and `as` reads the number out. Three refusals came with it, each
for a reason rather than for want of implementation:

- **Two members with one number.** `match` must name every member, and two
  labels of equal value cannot both be reached, so the enum could not be
  matched at all -- and the backend would have been handed a switch with a
  repeated case.
- **A number outside 32 bits**, which the width has no room for.
- **An integer converted to an enum.** That would produce values no member
  stands for, and every exhaustive `match` in the language rests on that being
  impossible. The conversion stays one-way on purpose.

See `tests/enum-values.abs`, `tests/enum-value-errors.abs` and
`tests/enum-value-syntax-errors.abs`.

**Defer: swept and clean.** Checked alongside: a defer registered in a loop
body runs at the end of each iteration, an inner block's defers run before the
enclosing scope's, a defer in a branch that is not taken does not run, a return
value is read before the defers run, and a defer inside a switch case runs when
the case does. `defer delete p` inside a loop does not satisfy the deletion of
an owner declared outside it, which is the conservative answer -- the loop may
run zero times. `tests/defer.abs` already pins LIFO order, `break`, `continue`,
exception propagation and a throwing deferred body.

**Interfaces and virtual dispatch: swept and clean.** Checked before the
shifts, and recorded here because the negative result is worth as much as a
find: an interface default method calling the contract it belongs to, a class
overriding that default, a three-level virtual chain, and a virtual call made
from a base-class method all dispatch to the most-derived implementation.
`tests/interfaces-codegen.abs` already pins the harder shapes -- one class
satisfying two interfaces that declare the same method, defaults inherited
through a diamond, and a class override winning over a default that another
default calls.

## 11. Beyond the list: where the language was slow

Found by running the benchmark suites rather than by probing for wrong answers,
and both are the same kind of defect as the rest: something the program pays
for that nothing in the source asks for.

- **Every dereference of a managed pointer took a global lock.**
  `absolute_managed_get` held a mutex while it read one slot, so an object
  reached through a parameter -- most code -- cost 22 nanoseconds per element
  access against 0.6 for the same array in a local. An insertion sort over
  100000 elements took 55 seconds instead of 1.5. Slots now live in chunks that
  are allocated once and never move, so a reader can hold a slot's address
  without a lock, and the fields it reads are atomic; the generation is read
  before and after the pointer so an expired slot is reported rather than
  handed over. Allocation, destruction and transfer keep the mutex. The same
  sort: **55.9 seconds becomes 15.8**.
- **Nothing told the optimizer that an element is not a field.** Every access
  through an object reloaded the array's data pointer and its length, and a
  bounds check that never changes could not leave the loop: 16 instructions in
  the inner loop where the same sort on a local array ran 6. Loads and stores
  now carry type-based alias information, and only where the type owns its
  storage -- a field, and an element of an array of primitives. A raw pointer,
  a value reference and an element of an array of structs stay undescribed,
  which means they may alias anything, which is always safe.
  **Insertion sort through a `Vector<int32>`: 3.59 seconds becomes 1.39**,
  against 1.73 for the same program in C++ with `std::vector` -- and the bounds
  check is still there.

Where that leaves the suites, as multiples of Absolute's time (higher is
slower, so `C++ 1.25x` means Absolute is faster):

| Suite | C++ | Java | JavaScript |
|---|---:|---:|---:|
| algorithm | 0.99x | 1.30x | 1.99x |
| pointer-object | 0.99x | 1.90x | 3.66x |
| array | 1.18x | 2.25x | 4.56x |
| collection | 0.69x | 6.33x | 4.93x |

What is still open in the collections is `vector-push-sum` and
`hashmap-insert-lookup`, both around 0.5x: growth policy and probing strategy,
which are library design rather than anything the compiler emits.

`tests/aliasing-guarantees.abs` pins the promise the second fix makes, by
writing through every pair of accesses that could overlap -- an array field
beside the fields that describe it, fields written through elements of an array
of structs, a raw view over a collection's buffer against its own indexer --
and reading the result back.

## 12. The method that worked

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

## 13. Environment note

During this work the container repeatedly reverted the working tree to an older
commit and deleted the build directory. Pushed commits were never affected, but
a stale build silently produced misleading results once — probe values read as
zero because the compiler predated a fix, which looked like a defect and was
not.

Check `git log --oneline -1` before trusting a probe, and rebuild after any
unexplained result.
