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

One, below, and it wants a design decision rather than a patch. Everything
else this section held has been closed, and each entry records what the fix
was, so a regression is recognizable rather than rediscovered.

### Open: a string has no lifetime, so every string a program builds is lost

`format`, `concat`, `substring`, `toUpper` and the rest allocate a buffer and
nothing ever frees it. `TypeNeedsCleanup` does not name `string`, so a string
is a `char*` that no scope owns:

```absolute
int32 i = 0;
while (i < 2000000) {
    string text = format("value {}", i);   // 32 bytes gone, every iteration
    i += 1;
}
```

Two million `format` calls end at **64.7 MB** of resident memory; 500000
iterations of `concat` + `toUpper` + `substring` reach 49.6 MB. A program that
runs for a while and formats anything grows without bound. Found by putting the
rest of the ownership corpus under AddressSanitizer, where the two tests that
print with `format` were the only ones that failed.

The fix is a decision about what a string is, which is why it is recorded
rather than guessed at:

- **Give strings the ownership the language already has.** The machinery is
  there -- `TypeNeedsCleanup`, scope cleanup, and the statement-level release
  built for owning temporaries -- and a string would be freed at the end of the
  scope that holds it. Every place a string can go (a field, an array, a
  return, a C-ABI boundary that documents the caller as the owner) has to be
  accounted for, and a wrong claim frees a pointer someone still holds.
- **Reference-count the buffer**, with a header behind the pointer so a
  `char*` still crosses the C boundary. Copies stay cheap to reason about, at
  the cost of a retain and release on every assignment.
- **Leave it as it is and say so.** Defensible for a compiler that runs and
  exits; not defensible for the servers and games the roadmap describes.

Until it is decided, `tests/temporary-owners.abs` and
`tests/aliasing-guarantees.abs` run under AddressSanitizer with the leak check
off -- they exist to catch a use-after-free and a double free, and these bytes
would drown that.

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

**Found while checking those measurements: the two targets disagreed about a
number.** `std.text.parseInt` answers zero natively when the number does not
fit, because `std::stoi` throws `out_of_range` and the wrapper catches it, but
the wasm shim had its own loop that accumulated into a signed int32 and kept
going. `"99999999999999999999"` was 0 natively and 1661992959 on wasm,
`"2147483648"` was 0 against -2147483648, `"-2147483649"` was 0 against
2147483647 -- a program parsing input it did not write got a different number
depending on where it ran. The shim accumulates wide and unsigned now and stops
at the same limit, asymmetric because -2147483648 is representable and
2147483648 is not. The rest of the text API was swept the same way afterwards
and agrees on every boundary: substrings past the end, negative bounds, code
points in Cyrillic and emoji, `indexOf`, `replace` to empty, `trim`, `repeat`
with a negative count. See `tests/text-number-parsing.abs`.

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

## 12. Beyond the list: what a generated program found in the backend

Nothing in the suite was probing whole programs under a sanitizer. The corpora
that existed generate one expression shape each -- integer mixing, floating
arithmetic, integer edges, ownership operations -- and none of them is compiled
with `--sanitize=address`. `tools/testing/codegen_fuzz.py` fills that gap: it
generates valid, self-checking programs -- a class with an array field and a
managed field, loops over computed indices, owners created and released inside
a loop, recursion -- builds each one at `-O0` under the sanitizer and again at
`-O3`, and asks three questions that are not a second implementation of the
semantics: did the sanitizer complain, did anything leak, and do the two builds
print the same number.

The first run answered "yes, leaked, 4 x N bytes" on every program.

- **Every local array in every program was allocated and never freed.** `new
  T[n]` produced its descriptor without ever being marked as an owner, so the
  release that already existed -- a scope frees the array storage it owns --
  never fired. There was no `free` in the emitted IR at all. It survived a
  green suite for two reasons: the programs print the right answer, and from
  `-O1` upwards the optimizer deletes an allocation nothing reads back, so the
  bytes only exist at `-O0`, which is not where anything was being checked.
  Two lines in the array branch of the constructor-call visitor, and the
  existing release connects.

- **An array produced inside a larger expression was dropped on the floor.**
  `map.toArray().length` allocated a snapshot that nothing named and nothing
  freed. The temporary-owner ledger that holds unbound managed owners until the
  end of a statement now also holds array buffers, which are released with
  `free` rather than the two steps `delete` takes. A *slice* is deliberately
  not registered: `return copy(values)[1:3]` keeps referring to the same
  buffer, so ending its life with the statement would return freed storage.

- **The base of an index was evaluated twice**, and this one is a wrong answer
  rather than a leak. The access visitor took a view of the base, then computed
  the element address, which took a view of the base again -- so for a variable
  it cost two extra loads, and for anything else it ran the expression a second
  time. `batch()[0]` called `batch()` twice, read its element out of the second
  array, and released the first; a function with a side effect had it happen
  twice, and the index and the release were looking at different memory. The
  leak is what exposed it: one allocation freed, two made. The address
  computation now takes the view the caller already has.

`tests/array-storage-release.abs` pins all three, and is the one sanitizer case
built at `-O0` rather than `-O1`, because at `-O1` a missing release is not
observable. The fuzzer runs as `absolute.codegen-fuzz`.

A sweep of the whole suite under `-O0` with leak checking on found no other
kind of leak: everything that remains traces to `absolute_string_*`, which is
the open defect in section 1.

## 13. Beyond the list: a test that could not overlap

Turning ThreadSanitizer on for generated code -- `--sanitize=thread`, plus a
copy of the runtime built for it, because TSan reasons about the whole program
-- found no races in what the language offers for concurrency: spawn, await,
task groups, channels, the concurrent primitives. It did find a test that was
asserting something its own setup prevented.

`tests/std-concurrent-primitives.abs` checks that an `RwLock` lets two readers
in at once, by spawning two readers, waiting two milliseconds, spawning a
writer, and asserting afterwards that the peak reader count reached two. The
lock prefers writers, which is the right choice and is why the check could
fail: a writer that got in between the two readers held the second one out, so
the readers never overlapped and the assertion was false through no fault of
the lock. Under the sanitizer, where everything starts slower, that happened in
5 runs out of 30.

The readers now wait for each other inside the lock, and the writer is spawned
only once both are in. The sequence the test describes is the sequence it runs:
0 failures in 40 under the sanitizer, and 0 in 40 with the machine's cores
deliberately loaded.

The clean runs are worth something only because one program is expected to be
dirty. `tests/thread-sanitizer-race.abs` has two tasks read-modify-write the
same `int64` through a raw pointer with no lock and no atomic, and the case
fails if the sanitizer does *not* report it -- an instrumentation that has
quietly stopped working produces exactly the same clean output as code that is
correct. The two racers rendezvous on an atomic before starting, because
without that they can run one after the other on a loaded machine and the
scheduler's own handoff orders their accesses, which is not a race and is not
reported.

## 14. Beyond the list: four more shapes in the fuzzer

The generated corpus started with one program shape. Four more were added --
virtual dispatch through an interface, exceptions leaving frames that hold
owners, closures that capture and escape, generics specialized at several
widths -- and each of the three defects below came out of the first runs.

- **Array storage was not zero-initialized**, which the documentation says it
  is (README: "Array storage is zero-initialized"). `new T[n]` called `malloc`
  and handed back whatever the allocator had in that block, so a program that
  read an element before writing it got one answer at `-O0`, another at `-O3`,
  and a third under AddressSanitizer -- three answers to a question the
  language says has one. That is how it surfaced: the fuzzer builds `-O0` under
  the sanitizer and `-O3` without it, and the two disagreed while both ran
  clean. Array *literals* were always zeroed -- an alloca and an explicit
  memset -- so only `new` was affected. It allocates with `calloc` now, which
  also means a large array costs zero pages from the operating system rather
  than a write over every byte. `tests/array-zero-initialization.abs` probes it
  the way it has to be probed: allocate, fill, release, allocate the same size
  again, because a first allocation usually sits on a fresh page that is zero
  anyway and proves nothing.

- **An index expression inherited the statement's access mode.** Writing
  `a[i % a.length] = v` was refused with "array property 'length' is
  read-only". It is read-only, and that has nothing to do with reading it to
  compute an index: the mode belongs to the target of the statement -- write
  for an assignment, address for `&a[i]` -- and it was reaching the index and
  the slice bounds as well. Both are reads now, whatever is being done to the
  element they select, and the refusals that should stay still fire
  (`tests/index-expression-context.abs`, and the error cases in
  `tests/array-errors.abs`).

- **A method of a generic class refused the conversions every other call
  accepts.** `Box<int64>.put(7)` was reported as "no overload accepts (int32)".
  The parameter really had been substituted to `int64`, but the method's symbol
  still carries the class's `T`, so the call went through generic unification,
  whose job is to bind type variables and which decides a pattern containing
  none by equality. A pattern with no variable in it binds nothing and now
  defers to the conversion rules, which is what the same call written against a
  plain class, a free function, or a generic *function* with an explicit type
  argument had always done (`tests/generic-member-conversions.abs`).

**And two in the harness rather than the language.** The differential runners
inherited their own standard input, so a test in the suite that reads to end of
file blocked on whatever was attached to the process that started the run: the
same corpus finished in seconds from one shell and sat until the timeout from
another. Every runner passes a closed stdin now. What a test answers must not
depend on who started it.

The second is worse, because it reported success. The suite differential
compares targets only when it has both a runtime to run the module and the host
that loads it; given one without the other it skipped the WebAssembly build for
every program, compared the optimization levels alone, and printed
`suite-differential=ok`. A run asked to compare two targets compared one and
said it was fine. Half the pair is refused now, with a message naming what is
missing -- and the axis is switched on in the test's own registration rather
than only being available from a command line, because until then the
comparison the corpus was added for had never actually run outside a hand
invocation. With it on: 151 programs, `-O0` against `-O3` against WebAssembly,
one nondeterministic program excluded by name, zero disagreements.

## 15. Beyond the list: an array that never released what it held

Found by giving the generated corpus a shape with a generic type inside a
generic type, whose natural spelling is an array of owners.

An owner stored in an array was never released, and nothing could make up for
it. The scope freed the array's buffer and no one walked the elements;
`delete a[i]` was refused because the element is a subscriber of the array; the
same thing written as a *field* was refused outright with
`E_ARRAY_FIELD_REQUIRES_OWNER`. So as a local it compiled, ran, leaked every
element, and the runtime aborted at exit reporting the leak. The construct
could not be written correctly at all:

```absolute
Cell*[] owners = { new Cell(1), new Cell(2) };   // leaks both
delete owners[0];                                // E_DELETE_SUBSCRIBER

Boxed[] boxes = new Boxed[2];                    // struct with a Cell* field
boxes[0].held = new Cell(7);                     // leaks
delete boxes[0].held;                            // E_DELETE_SUBSCRIBER
```

The mechanism was one branch of `EmitValueCleanup`, which released an array by
calling `free` on its buffer and returning. A stack literal never reached even
that: its owner is null, so the scope had nothing to do and the elements it had
just constructed went with the frame.

**An array now owns its elements**, the way an object owns its fields:
releasing one releases each element first, and `delete a[i]` stays refused
because the array is the owner. Two things make that safe. Only the owner of an
array does it -- a slice and a borrowed view share the buffer, and releasing
the same elements twice would be a double free -- and element ownership is
tracked separately from storage ownership, because a literal array has no
buffer to free while its elements are still the scope's to release.

That exposed a second question the old behaviour had hidden. `copy` duplicates
the buffer, not what the bytes in it refer to, so a shallow copy of an array of
owners produces two arrays holding the same handles; whichever is released
first destroys the objects the other still names. It was reachable -- a
function returning `copy(localArrayOfOwners)` handed back handles its own scope
had just expired, and the caller's first read hit "null or expired managed
pointer". `copy` of an array whose elements own something is refused now
(`E_COPY_OWNING_ELEMENTS`). Deep-copying was not chosen: what a deep copy of an
owner should mean is not a decision this builtin gets to make. Arrays of
ordinary values copy exactly as before.

`tests/array-element-ownership.abs` counts live objects through a `destroy()`
hook across 64 rounds, so a release that ran twice or not at all shows as a
count that does not return to zero; it is built at `-O0` under the sanitizer
for the same reason the storage-release case is.

**And the spelling that made it worse.** `new T*[n]` did not parse for any
pointer element type: the constructor parser built its type by hand and never
consumed a `*`, so the star was read as multiplication and the `[` after it as
the start of an expression. An array of owners could only be written as a
literal with a fixed element count -- exactly the case that leaked -- while the
type itself was usable everywhere else. The star is consumed now, and only when
an array suffix actually follows, so nothing that could be a multiplication
changes meaning: `new Point() * 2` still multiplies.

## 16. The method that worked

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

## 17. Environment note

During this work the container repeatedly reverted the working tree to an older
commit and deleted the build directory. Pushed commits were never affected, but
a stale build silently produced misleading results once — probe values read as
zero because the compiler predated a fix, which looked like a defect and was
not.

Check `git log --oneline -1` before trusting a probe, and rebuild after any
unexplained result.
