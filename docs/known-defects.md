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

None of this section's own. Everything it held has been closed, and each entry
records what the fix was, so a regression is recognizable rather than
rediscovered. One *missing feature* is open and lives in section 2, where a
thing that fails loudly belongs: a default parameter value is parsed and then
ignored.

The two were one defect seen from two sides -- **an array never releases what
its elements own** and **a string has no lifetime** -- and they were fixed as
one, which is why neither entry reads as a patch. Ownership is part of the type
now rather than something the analyzer reconstructs per symbol, so a container
drops its elements by asking what destroying one means rather than whether it
looks like a pointer, and a string is an ordinary value whose storage is
shared-owned. Section 15 keeps the record of the array, because the obvious fix
for it was written, merged and withdrawn in one session and what that cost is
the useful part.

The remainder both of them left -- **there is no copy that retains what its
parts hold** -- is closed too. An aggregate was copied shallowly, so a string
held by a field, or by a tuple element, which is the same aggregate reached by
a different spelling, could not be released at all: releasing it would kill
what the copy still names, which is the withdrawn attempt again. `TypeSemantics`
already said whether a type is `copyable`; what was missing was something that
*performs* the copy. `EmitValueRetain` is the mirror of the destructor walk --
a copy counts the parts it now names, a drop gives those counts back -- and it
runs in the six places a copy happens: a store, an assignment, a declaration, a
return, a temporary read out of a container, a scope. A tuple asks its elements rather
than a declaration it does not have; a value made only in order to pass in is
released by the statement that made it, because an aggregate parameter borrows.

The model, the order of work and what proves each step are in
`docs/ownership-kinds.md`; every step is done, pinned by
`tests/ownership-qualifier-generics.abs`, `tests/subscriber-pointers.abs`,
`tests/generic-body-ownership.abs`, `tests/open-ownership-qualifier.abs`,
`tests/array-element-drop.abs`, `tests/string-lifetime.abs`,
`tests/aggregate-copy.abs` and `tests/array-literal-owner.abs`.

### Fixed: a string had no lifetime, so every string a program built was lost

`format`, `concat`, `substring`, `toUpper` and the rest allocate a buffer and
nothing ever freed it. `TypeNeedsCleanup` did not name `string`, so a string
was a `char*` that no scope owned:

```absolute
int32 i = 0;
while (i < 2000000) {
    string text = format("value {}", i);   // 32 bytes gone, every iteration
    i += 1;
}
```

Two million `format` calls ended at **64.7 MB** of resident memory; 500000
iterations of `concat` + `toUpper` + `substring` reached 49.6 MB. A program
that ran for a while and formatted anything grew without bound. Found by
putting the rest of the ownership corpus under AddressSanitizer, where the two
tests that print with `format` were the only ones that failed.

The same loop now plateaus at **3.6 MB**, and so does the form that throws the
string away rather than keeping it.

#### What a string is

The second of the three options recorded here: **reference-count the buffer**,
with a header behind the pointer so a `char*` still crosses the C boundary.
A string is still a bare `const char*` -- `printf` takes it, the C ABI takes
it -- and immediately in front of its first byte sits a magic word and a count
of how many names are holding the bytes.

Shared rather than unique, because a string is a value: assigning one copies
the pointer, and two names for the same bytes is the ordinary case rather than
an error. That is the difference between a string and an owner, and it is why
`TypeSemantics` has a `copyable` as well as a `needsDrop`.

A literal is laid out the same way by the compiler, with the count set to a
sentinel meaning never released. That is what lets release work from a pointer
alone without guessing whether what it was given has a header at all; the magic
word remains as a backstop for a `const char*` that arrives from a plugin.

#### Four rules, none of them an exception

| | |
|---|---|
| a store | one more name, so it says so |
| a parameter | takes a count on the way in, gives it back on the way out |
| a return | hands its count to the caller |
| a scope | gives back what it was holding |

A string the expression made itself already holds one count and is not counted
again; the caller releases those at the end of the statement it made them for.

The split between the caller and the callee is the part that took three
attempts. Having the *caller* retain each argument breaks at the one boundary
that cannot play along: an external C function has no body to emit a release
into, so every call to `absolute_string_substring` and its neighbours leaked
the retain -- which is why `split` leaked four counts per call while the
pure-Absolute shapes were clean. Having the callee *borrow* instead fails the
other way: `Vector<string>.push` moves its parameter into a slot, and a
parameter owning nothing has nothing to hand on, so the array held a pointer
nobody counted. Each side holding its own count is what makes both work.

Pinned by `tests/string-lifetime.abs`, under AddressSanitizer with the leak
check on: 20000 iterations each of the reproducer above, a string returned from
a function, one passed in and handed back, a variable reassigned in a loop,
plus the standard library's own producers and `split`. One leaked allocation
per iteration would be thousands of reported leaks.

#### A string held by a field: the copy that was missing

An aggregate could not release its string fields, because copying one copies
the pointer without duplicating the count. Releasing a field on the strength of
a shallow copy kills what the copy still names -- the withdrawn array attempt
(section 15) in a new place -- so until there was a copy that says so, there was
no drop.

There is one now: `EmitValueRetain`, the mirror of the destructor walk. A copy
counts the parts it now names; a drop gives those counts back. It runs in six
places, which are the string rules plus the one an aggregate adds:

| | |
|---|---|
| a store into a slot | one more name for the bytes |
| an assignment | one more name, and the target gives back what it held |
| a declaration | the copy counts the parts |
| a return | counted on the way to the caller |
| a temporary | a value made only to read one field of, released with the statement |
| a scope | gives back what it held |

Two things had to stop counting as producers for any of it to work.
`unsafeArrayGet` hands back what the array already holds, and `move` of a
shared value hands back what the source still names; both are calls, so the
syntactic rule said they made their value. Every container's indexer is written
on top of the first, so an indexer's result was never counted and the caller's
copy released storage the container still had. `unsafeArrayTake` is
deliberately still a producer: it clears the slot, and that is what makes it
one.

The one that took longest to see was the smallest: an indexer read resolves to
the *container*, not to the indexer, so the analyzer's freshness flag answered
for the wrong symbol. Indexing something that is not an array is a call, and
that is how it is recognized now.

Three shapes were still outside all of it, and each was found by asking the
same question the class case answered.

A **tuple** has no declaration to look up. `SemanticsOfTypeName` answered from
`classes` and `structs`, and a `tuple<int64, string>` is in neither, so it
answered "nothing to release" and the string in it was lost -- the struct-field
hole reached by a different spelling. A tuple now asks its elements directly
(`DropKind::TupleValue`), and the walk is a `getelementptr` per element rather
than a field list. The local that holds one had to be told as well: a tuple
declaration takes the plain-value path, where only a string and a closure had
ever said they owned what they held.

A **value made only in order to pass it in** -- `take(make(1))`, `push(createHeader(...))`
-- was released by nobody. A parameter of an aggregate type borrows: it takes
no count and gives none back, so the count the value arrived with belongs to
the statement that made it, and the statement was not releasing it. Registering
it there exposed the other half immediately: `unsafeArraySet` retained a stored
*string* but not a stored aggregate, so the container that had been living off
the caller's uncollected count now held a slot nobody counted. A slot that is
released is a name that counts, whatever the element type is; both sides of
that are now the same rule, and `tests/aggregate-copy.abs` fails without either.

A **property's setter** was the one argument nobody released. A setter is a
call and its parameter borrows like any other, so `slot.entry = make()` leaves
the count with the statement that made it -- but the assignment path builds
that call itself rather than going through the one place every other argument
is told so, and an indexer's setter is written the same way. A named value was
fine; only one made on the spot was lost.

And **assignment** was neither half: `a = b` for a struct or a tuple copied the
bytes without counting what they hold and without releasing what `a` held, so
one name leaked and two names shared one count. A field assignment released the
old value but still did not count the new one. Assignment is a store; it now
reads as one.

Pinned by `tests/aggregate-copy.abs` -- a `Headers` class over a
`Vector<Header>`, cloned two thousand times, with a field read out of a
temporary, a tuple copied and borrowed two thousand more, a fresh struct handed
straight to a call, two thousand rounds of assignment including a struct
assigned to itself, and a property and an indexer taking one made on the
spot -- under AddressSanitizer with the leak check on,
where a use-after-free and a leak are both answers.

### Fixed: a value holding an owner was overwritten without being destroyed

The rules above are about values whose parts are *shared*, and they say nothing
about a struct holding a `Node*`: there is no such thing as a second name for a
unique owner, so such a value is not copyable at all. It moves, and
`E_RESOURCE_AGGREGATE_COPY` refuses everything else. What that leaves is the
other half of the same question -- where such a value may be handed on, and who
destroys what a name held before it took another -- and two of the answers were
wrong. Both were found by probing the shapes around the shared-value fixes,
which is also how the fuzzer shape for them came to be written.

**Assignment overwrote without destroying.**

```absolute
Owner target = makeOwner(0);
while (round < 2000) { target = makeOwner(round); }   // 1999 objects, nobody's
```

A field assignment released what the field held; a local's did not, because the
release was written for a value whose parts are counted and asked `copyable`
before emitting it. Overwriting is not copying: what the name held has to go
somewhere whether or not the new value is a second name for anything. The
runtime reported every one of them at exit, by handle -- but only if a program
wrote that loop, and none did.

**A conditional could not produce one.**

```absolute
Owner picked = left ? makeOwner(a) : makeOwner(b);    // E_RESOURCE_AGGREGATE_COPY
```

Whether an expression may hand on a resource-owning aggregate was decided by
asking whether the value's *symbol* is a callable. That answers for a direct
call and loses the fact for everything else, so a conditional of two calls --
two values, each produced by the arm that ran, neither of them named by
anything -- was refused with "use move(...)" about a value there was nothing to
move.

It is decided by whether the expression produced the value, and for a
conditional by whether both arms did. That distinction is the whole of it: for
a value whose parts are counted the backend can always make both arms produce
one, because the arm that only named the bytes can take a count of its own; for
one that travels with a role it cannot, because a second name is exactly what
does not exist. So `left ? first : second` over two named owners is still
refused, and correctly.

Making that judgement needed `TypeSemantics::copyable` to be true of the
analyzer as well. It was hard-coded `true` for every aggregate there while the
backend worked it out by walking the parts, and nothing had read it yet, so the
two halves had drifted without anything noticing. The analyzer walks the parts
now, the same way.

Pinned by `tests/owned-aggregates.abs` and `tests/owned-aggregates-errors.abs`,
and by the `owned` shape in the fuzzer.

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

One is open, below: a default parameter value. The four notations this section
was written for all now lex. They were honest syntax errors rather than wrong answers, but
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

### Fixed: handing a shared value from one name to another

Found by pushing strings into the standard library's own containers, where
these shapes meet. `Vector<string>` leaked one string per element, and
iterating one leaked another per pass.

**`move` cleared the source.** Clearing is what makes a move a transfer, and a
move is a transfer only where the destination cannot take a count of its own.
For a shared value it is a read -- it hands back what the source still names,
which is why the store that takes it counts it -- so clearing as well dropped
the source's own count on the floor. `unsafeArraySet(items, n, move(element))`
is how every container stores what it is given, so every container leaked a
string per element.

The condition is not `copyable` and not `needsDrop`: a struct wrapping a
`raw void*` with a `destroy()` hook is both, and copying it counts nothing --
there is one handle and the hook closes it -- so a move of one has to keep
clearing. What decides it is whether the copy takes a count, which is the same
question `EmitValueRetain` answers by walking the parts.

**The foreach loop's own variable was neither half of a name.** It never took a
count and it released one when the loop ended, so iterating a `string[]` twice
was a use-after-free -- the first loop released a count the array still needed.
Over a collection it was the other way round: the iterator's getter hands its
count over, the variable kept it, and the loop leaked a string per iteration.
The variable gives back what it held before taking the next element now, and
takes a count when the element is one the container still names.

Pinned by `tests/shared-value-handoff.abs`, whose loops all run twice over the
same container, because one missing count is only visible on the second pass.

### Fixed: a foreach loop kept nothing of what it iterated over

Three things, all in the same statement, and all invisible while an array
literal lived on the stack and a collection was always a variable.

**An array source was never released.** The comment in the backend claimed it
was -- "a temporary source has to outlive the loop and is released after it" --
and the scope for it was opened, but the registration every other borrowing
position does was missing, so nothing was ever put in that scope.
`foreach (x in makeArray())` leaked its array on every pass, and so did a loop
over an array literal once a literal became storage of its own.

**A collection source was evaluated twice.**

```absolute
foreach (int32 value in makeList()) { ... }   // two lists, one iterated
```

The value of the first evaluation was never read. What it did was run the
header a second time: two lists were built, the second was iterated, and the
first was left with nobody holding it -- which is also why it leaked, since
only the second was borrowed and registered. A side effect in a loop header
happening twice is the wrong answer underneath the leak.

Pinned by `tests/foreach-temporary-source.abs`, which counts the header's own
calls as well as running under AddressSanitizer.

### Fixed: a generic aggregate owned nothing, as far as the analyzer knew

```absolute
Cell<Node*> made(int64 n) { Cell<Node*> cell; cell.held = new Node(n); return cell; }
Cell<Node*> taken = made(1);
taken.held.value          // Absolute runtime error: null or expired managed pointer
```

Whether a type owns a resource was answered by walking the members of its
declaration -- and a generic's members are written in terms of its parameters,
so the walk asked about `T` and got the answer for a name nothing is known
about. `Cell<Node*>` was a type that owns nothing: not refused when copied, not
refused when returned by value, invisible to every ownership rule the analyzer
has. The same struct written without the angle brackets was refused by all of
them.

The backend substitutes, so it knew better and destroyed the owner where the
callee's local ended. Two halves disagreeing that way is worse than a leak --
the callee tore down what it was handing back, and the caller read a handle to
it. The walk substitutes now, and so does the one that answers `copyable`.

A second hole was underneath it: a declaration whose type is written with
generic arguments parses as a *variable* declaration rather than an instance
declaration, and only the second of those carried the copy refusal. So
`Cell<Node*> second = first;` was two names for one owner even once the walk
was right. Both paths carry it now.

Pinned by `tests/generic-resource-aggregates.abs` and its errors file.

### A field's initializer is refused rather than ignored

```absolute
class Counter {
    private int64 seen = 5;      // E_FIELD_INITIALIZER_UNSUPPORTED
}
```

It was parsed, collected, and then dropped: an object's storage is
zero-initialized and nothing ever ran what was written there, so `seen` read
back 0 and a string field read back null. A wrong answer with no diagnostic, on
notation anybody would write -- and a static field's initializer has always
worked, which is exactly what would lead someone to write the instance one.

Refused rather than implemented, because the part that is missing is a language
decision rather than a backend one. For a class it is clear enough: run the
initializers at the top of every constructor, after the base call, in
declaration order. For a struct there is no constructor to run them in -- a
struct's storage is made by declaring it, and an element of `new S[n]` is zeroed
with nothing to run at all -- and picking an answer there quietly is worse than
saying it is not there. `tests/field-initializer.abs` keeps the two forms that
do work; `tests/field-initializer-errors.abs` is the refusal.

Found while writing the test for that refusal: **comparing a string field of a
zeroed value was a segmentation fault.** `strcmp` reads through what it is
given and a zeroed string field holds a null, so `point.tag == ""` on a fresh
struct crashed. A name holding no bytes is the empty string, and it is compared
as one now. Printing had had the same guard for longer -- it substitutes
`<null>` there, which is a debugging affordance rather than an answer about the
value.

### Open: a default parameter value is parsed and then ignored

```absolute
int32 twice(int32 v = 3) { return v * 2; }
twice();       // E_NO_MATCHING_OVERLOAD: no overload of 'twice' accepts ()
```

The default is parsed, stored on the parameter, and never used: nothing fills
it in at a call site, so a call that omits the argument is refused as having no
matching overload -- which names the wrong thing, because the overload the
author wrote is right there. The same for a method and for a constructor,
where the message is `E_NO_MATCHING_CONSTRUCTOR`.

The standard library declares seven of them -- `Deque`, `Queue` and `Stack`
take `int32 initialCapacity = 8`, `std.fs` has four, and
`std.collections.channels` has one per element type -- so
`new Deque<string>()` does not compile although its own signature says it
should. Nothing depends on it today only because every call site in the
library and the corpus passes the argument explicitly.

**Not fixed here, and deliberately.** Refusing the declaration was written and
withdrawn: it is one line in the analyzer and it fails 28 tests, which is the
signal that the feature is expected rather than unwanted. Implementing it is a
language change of a size that deserves its own pass: the analyzer's overload
matching has to accept an arity shorter than the parameter list, `Symbol` has
to carry which parameters have defaults, and every call path in the backend
that builds its own argument list -- an ordinary call, a constructor call, a
base call -- has to fill the missing ones in. Restricting defaults to constant
expressions, the way a static field's initializer already is, would bound it:
every one in the standard library is a literal.

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

## 15. Fixed: an array that never released what it holds

Found by giving the generated corpus a shape with a generic type inside a
generic type, whose natural spelling is an array of owners. **Fixed on the
second attempt**; the record below is mostly about why the first one was
wrong -- it was written, merged, and withdrawn within the same session -- and
what had to exist before the right version could be written.

An owner stored in an array was never released, and nothing could make up for
it. The scope freed the array's buffer and no one walked the elements;
`delete a[i]` is refused because the element is a subscriber of the array; the
same thing written as a *field* is refused outright with
`E_ARRAY_FIELD_REQUIRES_OWNER`. So as a local it compiled, ran, leaked every
element, and the runtime aborted at exit reporting the leak.

An array releases what its elements own now, and the element type is what says
whether releasing one means anything -- `Node*[]` drops every element,
`sub Node*[]` drops none, `int32[]` emits no loop at all. The array never asks
what it is holding. Guarded by the owner pointer, which is the same bit that
already decided whether the storage itself is freed: a view into someone else's
allocation has none, and `move` clears the slot so a moved-from local does not
release what the destination now holds. Pinned by
`tests/array-element-drop.abs`, under AddressSanitizer with the leak check on.

### The literal needed an owner, not a different guard

```absolute
Cell*[] made = { new Cell(1), new Cell(2) };   // released, both
```

A literal small enough to live on the stack never got an owner pointer, and the
owner pointer is what the drop is guarded by -- so a literal's elements were
never released. Making the drop unconditional there instead was tried and is
worse: `move` signals a transfer by clearing that slot, so an unconditional
drop makes a moved-from local release what the destination now holds, which is
the withdrawn attempt's failure again in a new place. What the literal was
missing is an owner, so a literal is allocated rather than left on the stack.
A literal is also the only way to write a two-dimensional array of owners, so
that shape is covered by the same change.

It was allocated only when its elements owned something, which is the condition
the next section is about; every literal that is an expression is allocated
now.

### Fixed: the one place the two halves disagreed

Allocating a literal only when its elements own something left the analyzer and
the backend saying different things about what a literal is:

```absolute
Cell*[] made = { new Cell(1) };
Cell*[] moved = move(made);      // E_ARRAY_MOVE_REQUIRES_OWNER
```

The backend allocated that literal and it did own its storage; the analyzer set
`createsArrayOwner` only for `new T[n]` and `copy(...)`, so it refused the move.
Nothing was unsound -- the refusal is the conservative direction -- but the rule
underneath it could not be read off the source: whether a literal owned its
storage depended on its element type, so `move` would have worked on a literal
of owners and not on a literal of numbers.

**A literal is an owner.** Every literal that is an expression makes storage of
its own, and the analyzer says so, which is what lets one be moved, returned,
or put in a field. Storage that belongs to the frame is written as such:

```absolute
int32[] made  = { 1, 2, 3 };   // an owner; move, return, store it in a field
int32 fixed[3] = { 1, 2, 3 };  // the frame's, and the frame keeps it
```

That is the third of the three answers recorded here -- keep the distinction
but put it where the author can see it -- and it costs a `malloc` for the
view-form declaration. The shape the cost was recorded against,
`int32[] a = { 1, 2, 3 }`, does not appear once in the benchmark corpus or the
standard library; the sized form that does is untouched, because the
declaration provides the storage and never evaluates the literal as a value at
all. The same exception covers a global, whose storage is the module's.

Pinned by `tests/array-literal-owner.abs` (moved, returned, written into a
field, passed as an argument, two thousand rounds of each under
AddressSanitizer with the leak check on) and
`tests/array-literal-owner-errors.abs`, which is the sized declarator still
refusing to be moved.

### Why "an array owns its elements" is not the fix

It was implemented -- releasing an array releases each element first, the way
releasing an object releases its fields, with element ownership tracked
separately from storage ownership so a stack literal still releases what it
built. Every probe in this document passed. `Vector<Cell*>` broke in three
places, none of which the test suite covers:

- **Growth.** `push` allocates a larger array, copies the handles, and replaces
  the field. Releasing the old array then destroyed the very objects the new
  one now holds: pushing twenty elements left four alive, and the program went
  on using the other sixteen.
- **`pop`.** It hands the element to the caller and leaves the handle in the
  array, so releasing the vector destroyed what the caller was holding.
- **`toArray`.** The snapshot and the vector end up holding the same handles,
  so whichever is released first destroys the other's elements.

Each could be fixed with an explicit transfer -- an intrinsic that moves
elements out and empties the source. What cannot be fixed that way is the
reason they all happen: **in this language a copied managed pointer is a
subscriber, not a second owner**, and an array of managed pointers has no way
to say which of the two it holds. `toArray` is the clearest case: it must
return subscribers, and the type it returns is the same `T[]` that would own.
Without a type-level distinction between an array of owners and an array of
subscribers, "an array owns its elements" is a rule the type system cannot
carry.

So the change was withdrawn. What it replaced -- a leak the runtime reports
loudly at exit -- is worse than a leak and better than what the fix produced: a
silent premature destroy in the standard collection, correct-looking output and
all. A loud leak is the safer of the two to live with while the design is
decided.

### What was kept

- **`copy` of an array whose elements own something is refused**
  (`E_COPY_OWNING_ELEMENTS`). Two arrays holding the same handles is wrong under
  any rule about who releases them, so this holds either way.
- **`new T*[n]` parses.** The constructor parser built its type by hand and
  never consumed a `*`, so the star was read as multiplication and the bracket
  after it as the start of an expression. An array of pointers could only be
  written as a literal with a fixed element count. The star is consumed only
  when an array suffix follows, so `new Point() * 2` still multiplies.

### What deciding it took

Both of the answers written down here were wrong about the size of the thing.

- **A type-level distinction** between an array that owns its elements and an
  array of subscribers, so `toArray` can return the second and `push` can move
  into the first. This was the real fix, and it is `sub T*[]` -- but a type for
  it was not enough on its own. `toArray` also had to be able to *say* it in a
  generic body, which is `sub T`, and the rules had to run there at all, which
  is §18. And `push` moving into the first needed operations that transfer an
  element rather than read it: `unsafeArrayTake`, `unsafeArrayMove`,
  `unsafeArrayDrop`.
- **Or: refuse owning element types outright.** Not needed, and it would have
  made `Vector<Node*>` unwriteable for the sake of a defect that turned out to
  be closable.

What actually closed it, in the order it had to happen: ownership became part
of the type (`OwnershipKind` in the canonical name), no qualifier was lost in
substitution, `Copy`/`Move`/`Drop` became type-driven (`TypeSemantics`), `sub
T*` became spellable, the rules started running inside generic bodies, and only
then could an array drop its elements without a container getting it wrong.

## 16. Fixed: a conditional only worked on numbers and handles

Found while probing the shapes the aggregate copy had just been taught, by
writing one of them behind a `?:`.

```absolute
string chosen = cond ? "yes" : "no";   // Error: binary operator requires numeric operands
```

The backend merged a conditional's two arms at a type it got from
`CommonNumericType`, which answers for two numbers and fails for anything
else. A handle survived because a managed pointer is an `i64` and looks like a
number; a string, a struct and a tuple did not. The message named binary
operators, and it arrived with no line and no column, because the analyzer had
already accepted the program -- so what an author saw was the backend refusing
something the language allows.

The type of the merge is the type of the expression, which the analyzer works
out from both arms and which every other consumer already reads.

That leaves the question the numbers never raised: **who is holding the value.**

```absolute
string mixed = cond ? format("n{}", i) : kept;
```

One arm produced the bytes and holds a count; the other named bytes something
else holds. Nothing downstream can ask which path ran, so the two are made to
agree at the merge -- an arm that borrowed takes a count of its own -- and the
conditional then reports that it produced its value, the way a call does. Every
rule about storing, passing and releasing one applies unchanged after that.

Pinned by `tests/conditional-values.abs`: literals, names and calls in both
positions, an aggregate, a tuple, and the borrowed name checked at the end to
show it still holds its bytes -- two thousand rounds under AddressSanitizer
with the leak check on, where one arm counted and the other not is a leak on
one path and a use-after-free on the other.

## 17. Fixed: two array slots that never counted what they were given

Found by the fuzzer, and by the half of it that is not the sanitizer.

A new shape -- `counted` -- carries a value whose parts are counted through
every place a copy happens, because the shapes that existed went nowhere near
it: `aggregates` builds tuples of numbers, `text` builds strings with no
aggregate around them, `handles` owns objects rather than sharing anything. It
found two more slots that took a name without counting it.

```absolute
Entry kept = entryOf(2);
Entry[] literal = { entryOf(1), kept };   // kept's strings, uncounted
Entry framed[2] = { entryOf(1), kept };   // and nothing walked these at all
```

A store into an array slot had been taught to count what it stores, but only
where `unsafeArraySet` writes it. Both array *initializers* have their own
store loop, and neither said anything -- so releasing the literal took the
strings the local still named.

The sized declarator was missing the other half too. Element cleanup was
guarded by the owner pointer, which is the right question for a *view*: a view
into someone else's allocation has none, and releasing through it would drop
what the real owner still holds. A frame array is not a view. Its storage
belongs to the frame and is not freed, and what its elements hold is still the
name's to give back -- so `Entry fixed[2] = { ... }` in a loop lost one string
an iteration.

**What caught the first one was not the sanitizer.** The bytes were freed and
handed straight back out by the next allocation, so every read landed in valid
memory and AddressSanitizer had nothing to say. `-O0` and `-O3` printed
different text, which is the check the fuzzer runs alongside the sanitizer and
the reason it runs both:

```
DISAGREEMENT seed=771017 shape=counted: {'O0': '313591', 'O3': '67069'}
```

Every optimization level gave a different answer, which is what reading storage
that has been handed to someone else looks like when nothing traps.

Pinned by the `counted` shape and by `tests/aggregate-copy.abs`, which now
writes a name into a literal's slot, a sized declarator's slot and a string
slot two thousand times and checks the name still holds its bytes.

## 18. Fixed: ownership rules were not enforced inside a generic body

A generic body is analyzed **once, with its parameters unsubstituted**, and
never again against any instantiation. That is not a gap in one check; it is
every ownership rule at once, because all of them ask whether a type is a
pointer and inside the body the type is `T`.

The proof that no per-specialization analysis exists is one probe:

```absolute
class C<T> { public T item; public int32 narrow() { int32 y = item; return y; } }
```

`C<int32>` and `C<string>` report the identical error at the identical place, in
terms of `T`. Nothing is ever re-checked.

Three symptoms, all from the same three-line class:

```absolute
class Sieve<T> {
    public T held;
    public void keep(T given) { held = given; }
    public T leak() { return held; }
    public void wipe() { delete held; }
}
```

| Written | Without the generic | Inside `Sieve<T>` at `T = Node*`, before |
|---|---|---|
| `held = given;` | `E_RESOURCE_FIELD_REQUIRES_OWNER` | accepted, and **the object is destroyed during construction** |
| `return held;` | `E_MANAGED_RETURN_REQUIRES_OWNER` | accepted, and the backend then fails with an internal "unknown variable" message |
| `delete held;` | `E_DELETE_SUBSCRIBER` | only a complaint about the shape of `T`, not the rule |

The first was silent corruption: `new Sieve<Node*>(move(owner))` left the
object already gone, because the constructor's parameter has the substituted
type, the backend releases it at the end of the constructor, and the field it
was stored into is not a transfer. The second was worse in a different way -- a
program the analyzer accepted and the backend could not compile, reported as an
internal condition rather than as anything the author can act on.

### How it was closed

Not by analyzing every body again for every instantiation -- the direct answer,
and a large piece of work. The single pass keeps analyzing the body once, but
where a rule would have run and could not, it now **records what it saw**: the
shape (`a field stored from something that is not a fresh owner`, `a field
handed back to the caller`, `a value released`), the type as written -- `T`,
the only thing that can be substituted later -- and the location. Each
instantiation substitutes its own arguments into those facts and asks the rule
then, reporting at the body's own line and naming the instantiation that was
judged.

`Analyzer::RecordGenericBodyFact` records; `Analyzer::CheckGenericBodyFacts`,
called where a generic type is first resolved, judges. Facts are recorded only
for a bare parameter of the type being analyzed -- anything concrete was
already judged by the rules themselves -- and deduplicated by location, because
a class can be re-entered.

The shape rule moved the same way, in the other direction. `delete held` inside
the body used to be refused outright, on the grounds that `T` is not a pointer.
That refused it for every `T`, including the ones it is written for. A bare
parameter is not a non-pointer; it is a name that is not a type yet, so the
question is now asked one level up, where the answer is known.

The result is the whole point of the model, on one body:

| Instantiation | `delete held` |
|---|---|
| `Sieve<Node*>` | `E_DELETE_SUBSCRIBER` -- released by its own object, not by its owner |
| `Sieve<sub Node*>` | `E_DELETE_SUBSCRIBER` -- borrows an object it does not own |
| `Sieve<weak Node*>` | `E_WEAK_DELETE` -- observes an object it does not own |
| `Sieve<int32>` | `E_DELETE_REQUIRES_POINTER` |
| `Sieve<raw Node*>` | accepted |

Pinned by `tests/generic-body-ownership-errors.abs` (five diagnostics, one
body, each naming its instantiation) and `tests/generic-body-ownership.abs`
(the accepted side: a container over `sub Node*` that releases nothing, and the
owner outliving it).

What this does not do is analyze the body per specialization. A rule that has
no fact recorded for it still does not run inside a generic. The three that
mattered here do; adding a fourth is adding one recording site, not another
mechanism -- which is what the array-slot rule below turned out to be.

### The check was order-dependent, and that hid the size of it

Checking each instantiation where the type was first resolved was wrong in a
way that passed every test: a generic instantiated **above** its own
declaration -- in a later file, or lower in the same one -- had no facts
recorded yet, so the check found nothing to say and said nothing. Every
`import` was in that position. The check runs once, after the whole program is
analyzed, over every instantiation the program reaches, and is now independent
of the order the source happens to be in.

Closing it made the real size of the finding visible: **the standard
collections are not written for elements that own something.** `Vector<Cell*>`
reports fourteen diagnostics across three classes -- `Vector` itself, plus
`VectorIterator` and `VectorBuilder`, which the closure over member signatures
reaches whether or not the program calls `iterate()` or `builder()`. Each of
them moves elements by reading a slot and storing what it read.

Nothing in the suite instantiated a collection over an owning element, which is
why none of this had ever been seen. Two of the three are fixed by giving the language a way to say *the subscriber
form of `T`* -- `sub T`, a qualifier in front of a name that is not a pointer
yet, applied when `T` becomes something and nothing at all when `T` owns
nothing. `VectorIterator`'s snapshot is `sub T[]`, and `Vector`'s `first`,
`last` and `toArray` hand back the subscriber form, because reading an element
is not the vector giving it up.

`VectorBuilder` is the one that saying something weaker cannot fix, and it is
what `tests/std-collections-owner-elements-errors.abs` now pins. It is a
staging owner: `add(T)` gives it an element and `finish()` hands every element
to a new `Vector<T>` that owns them -- but it is constructed from a live
vector's storage, so over an owning element its first act duplicates what that
vector still holds, and borrowing instead would have `finish()` give a vector
of owners handles it never owned. Whether `builder()` should drain the vector,
be refused for owning elements, or not exist is a decision about what the
method means, not a patch. It is reached without being called: a generic
instantiates the return types of members no line uses.

The container written the way the model requires, over the same element type,
is `tests/vector-owner-elements.abs`.

## 19. Beyond the list: what is left for undefined behaviour

The open TODO asked for UBSan over generated code. Before building anything,
the question worth answering is what UBSan would find, so the emitted IR was
read for the constructs it exists to catch:

| Construct | Emitted by code generation | Why |
|---|---|---|
| `nsw` / `nuw` | none | arithmetic wraps, and wrapping is defined |
| `sdiv` / `srem` unguarded | none | the divisor is checked at runtime |
| shift past the width | none | checked at runtime, refused by the analyzer where it is constant |
| `fptosi` / `fptoui` | none | conversion saturates |
| `!tbaa` | yes | **the one thing code generation adds** |

The only `nsw` in the sample was LLVM's own inference on a `snprintf` size, and
the only `noalias` was on libc declarations. So the checks a frontend would have
to emit for UBSan mostly duplicate checks the language already performs.

What is left is type-based alias information, and it is the dangerous kind. A
tag is a promise to the optimizer that an access through one type cannot reach
storage of another, and the optimizer believes it. A wrong promise is not a slow
program: it is a load hoisted out of a loop that a store inside the loop really
did change. **Neither `-O0` nor a sanitizer can see it** — at `-O0` the metadata
is present and unused, so the program is right, and AddressSanitizer answers a
different question, since a hoisted load reads memory it is perfectly entitled
to read.

The only thing that shows a false tag is the same program built twice at the
same optimization level, one told what may alias and one told nothing, because
telling nothing is always safe. `--no-type-alias-info` is that switch and
`tools/testing/alias_differential.py` is the comparison, over the suite at `-O2`
and `-O3`: 304 programs, zero disagreements.

**That number meant nothing until the check was shown to have teeth**, so the
tags were deliberately falsified -- a distinct node per access site, which
claims that no two accesses anywhere can overlap -- and the differential run
again. It found nothing. Two probes explained why: a managed dereference goes
through an opaque runtime call that clobbers memory on its own, so the tag has
no leverage there at all, and no program in the suite had the shape where it
does. The shape that does is two accesses to *elements of the same array* in a
loop:

```absolute
elements[pass] = elements[pass] + 100;
folded += elements[0];
```

With the tags falsified this answers 0 instead of 800, at `-O2` and above only:
the optimizer keeps `elements[0]` in a register across a store that really does
write it. That shape is now in `tests/aliasing-guarantees.abs`, along with the
same question for a field written through one name and read through another,
and with it the differential reports the falsified build as a mismatch on both
levels. The sabotage was reverted and the clean build agrees again.

The rest of the file was already checking the opposite direction -- that a true
claim stays true. These two are the only place in the suite where a *false*
claim is observable, which is what gives the differential its power; a change
that removes them would leave the check green and empty.

## 20. The method that worked

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

## 21. Environment note

During this work the container repeatedly reverted the working tree to an older
commit and deleted the build directory. Pushed commits were never affected, but
a stale build silently produced misleading results once — probe values read as
zero because the compiler predated a fix, which looked like a defect and was
not.

Check `git log --oneline -1` before trusting a probe, and rebuild after any
unexplained result.
