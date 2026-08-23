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

One, and it is a decision rather than a patch: what `{}` means for a double.
Section 22 found two more faces of it -- wasm prints no number at all, and the
two targets write JSON numbers differently -- and they are recorded there,
with the one component that answers all three. The entry this section opened
with -- an indexer that could not say its getter borrows and its setter takes
-- is fixed, and it is the entry below the open one.

### Open: printing a double prints six significant digits of it

```absolute
println(format("{}", 123456789.0));   // 1.23457e+08
println(format("{}", 1.0 / 3.0));     // 0.333333
println(format("{}", 0.1 + 0.2));     // 0.3
```

Code generation lowers a `float` or a `double` to `printf` with `"%g"`, whose
default precision is six significant digits. So the text a program prints is
not the value it holds and does not read back as that value. An integral double
above a million comes out in exponent form. And the third line -- the one a
person prints precisely in order to see that a double is not a decimal --
prints `0.3`, which is the one thing the value is not.

This is recorded rather than changed because it is a choice about what the
language's one formatting placeholder promises, and both answers are defensible:
`%g` at six digits is what C's `printf` and C++'s `iostream` default to, and
the shortest text that round-trips is what Rust, Go and Python print. Changing
it changes the output of every program that prints a real number.

Section 22 found the rest of this, and it makes the choice less free than it
looks here. The wasm target prints the literal text `%g`, because its
freestanding formatter has no float directive at all; and in JSON, where the
six digits are data loss rather than a house style, native and wasm write
different text for the same number. All three are the same missing piece -- one
shortest-round-trip routine both runtimes call -- and section 22 says what
building it involves.

If it is decided in favour of the round trip, the shape of the fix is known.
`"%.17g"` needs no runtime and always round-trips, but it prints `0.1` as
`0.10000000000000001`, which is precise and unreadable. The shortest form needs
`std::to_chars`, which means the value must become text before it reaches
`printf`: emit the conversion at the call site into an `alloca`'d buffer and
pass `%s`, one buffer per argument, so two doubles in one `println` cannot
share one. `PreparePrintable` in `codegen_runtime.cpp` is where both live, and
`absolute_string_builder_append_double` in `string.cpp` is the second caller
that would have to agree.

### Fixed: an indexer is a projection, so its getter borrows and its setter takes

```absolute
class Source {
    public Cell* made { get { return new Cell(1); } }            // released
    public Cell* this[int32 i] { get { return new Cell(2); } }   // now refused
}
Cell* stolen = cells[0];   // now refused: `c[i]` is `sub Cell*`
```

A property getter that produces an owner is a call that produced an owner, and
the temporary it makes is released with the statement -- that was fixed first,
and `tests/accessor-owner-temporary.abs` pins it. An indexer could not be given
the same answer, because an indexer has **one type for both of its accessors**:
the setter is handed a `T`, so the getter had no way to say `sub T`. The
backend then had one flag for two cases and had to pick. Treating the result as
a temporary owner destroyed elements the container still held --
`tests/vector-owner-elements.abs` and `tests/deque-owner-elements.abs` both
aborted with "null or expired managed pointer" -- and treating it as a borrow
leaked every indexer written as a factory. It was recorded rather than guessed
at, because which one is right is a decision about what an indexer *means*.

The decision is that **an indexer is a projection onto a cell the container
keeps, not a factory**: `c[i]` names storage that already exists. So the one
written type is read two ways -- the getter hands back `sub T`, because reading
a cell does not give it up, and the setter is handed `T`, because writing one
does. It is the same fixture C++ (`T&`), Rust (`&T`) and Swift (a borrowing
subscript) settle on, and for the same reason: a container cannot both own an
element and hand it out. Giving one up is a different operation, and it is
spelled differently -- `take`, `pop`, `removeAt`.

`Analyzer::IndexerBorrowProjection` is the whole rule, and it is applied in two
places: to the getter's declared return type, and to the type of a read through
the brackets. A write keeps the written type, because a write is the setter's
call. It is a no-op wherever there is nothing to weaken -- a number, a struct,
an array descriptor -- so at `T = int32` nothing about a container changes.
`CallableReturnType` records the projected type on the type expression itself,
so the backend reads one answer from one place rather than applying the rule a
second time and having to agree.

Three things follow, and each is a refusal that used to be silent:

- An indexer getter written as a factory is refused where it is written
  (`E_SUBSCRIBER_RETURN_LOCAL_OWNER`), instead of leaking one object per read.
- `Cell* x = c[i];` is refused (`E_INITIALIZER_TYPE_MISMATCH`), because the
  container still holds the element and this would be a second owner. `sub
  Cell* x = c[i];` is how it is written, and `c.takeAt(i)` is how the element
  is given up.
- `delete c[i];` is refused (`E_DELETE_SUBSCRIBER`).

For an **open** parameter the question cannot be answered in the body: `T key =
vec[i];` duplicates ownership at `T = Cell*` and is the same type twice at `T =
int32`. So it is not answered there. `IsAssignable` lets `T` take a `sub T`
inside an open body, and every site that binds a value to a name -- a
declaration, an assignment, an argument, a return -- records
`GenericBodyFact::Shape::BorrowsAsOwner`, which each instantiation replays.
`Slots<int32>` is silent and `Slots<Node*>` reports `E_BORROW_BOUND_AS_OWNER`
with the line inside the generic body. This is the same machinery the ordering
and interface rules already use, and it is why the standard library's generic
algorithms still compile: they are instantiated at parameters that own nothing.

Two defects surfaced only because the rule made a setter mean something:

- **A property or indexer setter was never told whether it was handed an owner
  or a borrow.** `EmitPropertyAccessor` built its call without ownership flags,
  so every setter parameter was told "borrowed", and `set { slot = move(value);
  }` -- the only way a setter can take -- aborted at run time with "Ownership
  operation requires an owner argument" on a value the caller had just
  produced. The flags run parallel to the arguments now, exactly as they do
  for an ordinary call, because a setter *is* an ordinary call.
- **The subscriber return rule reported two different mistakes in one
  sentence.** Handing back a fresh allocation and handing back a name for a
  local owner are not the same error, and a factory getter -- now the most
  likely way to hit this rule -- read as "returning a subscriber to a local
  owner" about a program with no local owner in it.

`tests/indexer-borrow-projection.abs` runs a generic container at both an
owning and a non-owning parameter, under AddressSanitizer with leak detection,
and counts destructor calls; `tests/indexer-borrow-projection-errors.abs` pins
the four refusals. `tests/accessor-owner-transfer.abs` was written under the
old model -- its indexer was a factory -- and now says what an indexer is.

A **closure** is not in this position, and both accessors hand one over
correctly now. A closure is counted rather than owned uniquely, so a getter
gives back a count whatever its body does -- it returns a fresh closure, or it
retains the one it read, because that is what a callable return means -- and
there is no borrowed case for the type to be unable to express. Both accessor
paths used to leave that count behind, so passing `source.maker` or
`pipeline[i]` straight into a call leaked the closure and its environment once
per call. `tests/accessor-owner-temporary.abs` covers both.

Everything else this section held has been closed, and each entry records what
the fix was, so a regression is recognizable rather than rediscovered.

A short list is not the same as no defects, and this file should not be read
as one. Most of what is recorded below was found *after* the list first
emptied, by running programs rather than by reading it -- the standard
library's own containers under AddressSanitizer, with every string built at
runtime. The last such sweep found twenty-four, six of them in the same gap, and
section 1's last three entries are that sweep. The one thing it found and did
not fix -- an indexer that could not say its getter borrows -- is fixed now, at
the top of this section: it was a decision about what an indexer means rather
than a patch, which is why it waited to be decided. Sections 20 and 22 are the
newest sweeps and the ones furthest from the language itself -- 22 is where to
start, because it names a shape rather than a module; section 5 says where not
to look again.

Nothing is open elsewhere either. The two things that were -- a name in the
shared type model answering two questions, and what slicing a temporary array
left behind -- are recorded as fixed in sections 2a and 15.

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
a copy counts the parts it now names, a drop gives those counts back.

Where it runs is the whole of it, and finding the places took longer than
writing the walk. A copy counts what it takes at a store into a slot, an
assignment, a declaration, a return, a temporary read out of a container, a
loop's iteration variable, and a property's generated accessors; a name gives
its counts back at a scope, a slot it overwrites, a field it overwrites, a
temporary the statement made, and a loop variable taking the next element. Each
of those was a separate entry below, because each was a place the rule had not
reached rather than a rule that was wrong. A tuple asks its elements rather
than a declaration it does not have; a value made only in order to pass in is
released by the statement that made it, because an aggregate parameter borrows.

The model, the order of work and what proves each step are in
`docs/ownership-kinds.md`; every step is done, pinned by
`tests/ownership-qualifier-generics.abs`, `tests/subscriber-pointers.abs`,
`tests/generic-body-ownership.abs`, `tests/open-ownership-qualifier.abs`,
`tests/array-element-drop.abs`, `tests/string-lifetime.abs`,
`tests/aggregate-copy.abs`, `tests/array-literal-owner.abs`,
`tests/owned-aggregates.abs`, `tests/shared-value-handoff.abs`,
`tests/temporary-array-elements.abs`, `tests/auto-property-storage.abs`,
`tests/conditional-owners.abs` and `tests/foreach-temporary-source.abs`.

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

### Fixed: a property's storage was written and read uncounted

`public H value { get; set; }` gets its storage and both accessors from the
compiler, and neither of them counted anything.

The **setter** released what the field held and stored what it was given
without counting it, so the field named parts it never counted; reading the
property once the assigned value had gone read freed bytes. For a string it
happened to balance -- the accessor's parameter takes a count on the way in, as
every parameter does, and this one never gave it back, so the count the field
was missing and the count the parameter leaked were the same one. Two mistakes
cancelling, which is not the same as being right: the moment the type was an
aggregate holding a string, only one of them was there.

The **getter** handed the caller a copy of the field's bytes uncounted, while
the caller releases what a call produced. Every read took a count off the
field, so reading a string property five hundred times freed what the field was
still holding.

Both are the rules the hand-written forms already follow: a store into a field
counts what it takes and gives back what it held, and a return counts the copy
it hands to the caller. The accessor's own parameter now gives its count back
the ordinary way, by its scope closing.

Pinned by `tests/auto-property-storage.abs`, which assigns from a name that
then goes away, writes two thousand times over, assigns a property from itself,
and reads two thousand times.

### Fixed: a message that named the wrong thing twice

```absolute
class Holder {
    private Leaf* child;
    public Holder() { child = new Leaf(1); }
    public void destroy() { delete child; }   // refused, rightly
}
```

Refusing is correct: a field holding a managed pointer is released with the
object that holds it, so deleting it by hand would destroy the object twice.
The message was `managed subscriber cannot be deleted; delete its owner
instead`, which names the wrong thing twice -- the field is not a subscriber,
and there is no other owner to go looking for. It cost two probes here before
the refusal was recognized as correct rather than as the defect.

A field now gets its own message and code (`E_DELETE_OWNED_FIELD`) saying what
releases it. Pinned by `tests/delete-owned-field-errors.abs`.

### Fixed: a conditional answered for whichever arm ran last

The counting half of a conditional was made to answer for the merge -- each arm
takes a count of its own so both hand the same thing on. What the arms said
about what they *made* was not: `valueCreatesManagedOwner` and its neighbours
were left holding whatever the second arm's evaluation set, and one of them
held an SSA value from that arm's block.

```absolute
Shape* shape = left ? new Bigger(3) as Shape : new Square(3) as Shape;
```

did not compile at all: the object behind a managed handle is cached so later
reads need not go through the handle table, and caching it here stored a value
from one path in a place both paths reach -- "instruction does not dominate all
uses", an invalid function rather than a wrong answer.

The other two are quieter. The result claimed to own what only one arm made, so
`cond ? kept : make()` would have had the local destroy an object a name still
holds; and it claimed to own nothing when the false arm happened to be a plain
value, dropping what the true arm made.

The merge owns something only if both arms produced one, and it holds no value
that belongs to a single path: an array's owner is read out of the descriptor
the merge itself produced. Pinned by `tests/conditional-owners.abs`.

### Fixed: a temporary array released its storage and nothing in it

Found by the `flowing` shape the moment it was written -- a generated program
that carries counted values across statement boundaries rather than through
expressions.

An array an expression made and nobody kept was registered as a bare buffer and
released with `free`. That let go of the storage and of nothing in it, so every
shape that reads something out of an array without keeping the array leaked one
count per element that has one:

```absolute
makeArray().length            // a field of it
makeArray()[0]                // an element of it
foreach (x in makeArray())    // a loop over it
copy(makeArray())             // a copy, which had just counted every element
```

It is registered as the value it is now, and the walk that releases it is the
one the type already describes -- `DropKind::ArrayStorage`, elements first and
then the buffer, the same walk a scope-owned array gets. `copy` releases its
temporary source the same way rather than freeing it.

#### And a slot never gave back what it held

The same defect one level down, found by probing an indexer on the way to
something else. A store into an array slot counted what it took and never
released what was already there:

```absolute
while (round < 2000) { slots[0] = format("w{}", round); }   // 1999 strings, nobody's
```

Every write to an occupied slot leaked its previous occupant -- through
`unsafeArraySet`, through `a[i] = v`, and through any indexer written on top of
either, which is every container's. It stayed invisible because a container
fills empty slots: `push` writes where the storage was zeroed, growth memsets
what it moved out of, and a test that fills an array fills each index once.

A slot is a place the array releases, so it gives back what it held before
taking another -- the rule a local and a field already followed. The incoming
value is counted first, through a spill, because `a[i] = a[i]` is the two being
the same bytes.

#### And then slicing one, which was the same defect one level in

Releasing it through the *view* is releasing part of it. `copy(makeArray()[0:2])`
gave back the two elements the slice names and freed the whole buffer, so the
third element's string was never given back -- one count per element the
program sliced away.

The information had to live somewhere, and there were two places it could:
in the array descriptor, which would widen every array everywhere including
the C ABI, or in the *ownership record* -- which is the set of places that
release an array and nowhere else. The second was chosen and is what the code
does now: what an owner covers travels beside the owner pointer, from wherever
the array was made, through every slice and row that narrows the view, to
whoever ends up releasing it. A temporary registers the extent it was given; a
name that keeps a slice remembers it next to the owner it holds; `copy`
releases its source through it. The descriptor is untouched, so nothing about
how an array is passed changed.

A slice of a *named* array is unaffected, and has to be: the name still owns
the storage, the slice is a view into it, and neither the view nor anything
made from it releases anything.

Pinned by `tests/temporary-array-elements.abs` and by the `flowing` shape.

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

### Fixed: an accessor is a call, and six places did not know it

A property and an indexer are calls written without parentheses. Six defects
lived in the gap between that fact and code that asked for the *shape* of an
expression instead. They were found by running the standard library's own
containers under AddressSanitizer with strings built at runtime -- a literal is
never released, so a corpus of literals proves nothing -- and each one is now a
test that fails when its fix is removed.

| What was wrong | Where it showed |
|---|---|
| `copy` of an array counted an element that *was* a string, not one that *held* one | `Map<string, string>`: `ensureUnshared` copies the entry array, so iterating and then removing a key read a key the snapshot had already released |
| an indexer's index argument was never released | `table[format("key-{}", i)]` leaked one string per lookup -- per use, not per program |
| a closure's environment counted a captured closure and nothing else | a captured string was freed by the scope it came from; the environment held the freed bytes for as long as the closure lived |
| a lambda written as one expression closed no scopes | every count its parameters took on the way in was kept for the life of the program |
| an array from a property or an indexer transferred no owner | `holder.rows` handed back storage nobody was recorded as holding |
| a `T*` from a property or an indexer was a subscriber | `delete` on it was refused, and the runtime's own leak check named the handle at exit |

Two more came out of the same sweep and are part of the fixes above: a closure
had no size, so `new func<int32, int32>[3]` failed to build at all while a
field of the same type had always worked; and a closure read out of a container
was counted twice, because "did this expression produce its value" had two
answers for a closure -- the analyzer's and a backend flag's -- which disagreed
exactly where `Vector<func<string, string>>` keeps its elements.

And one that is next door rather than in the gap, found by the same probes.
**A temporary belongs to the statement that made it, and two ways out of a
statement never reached its end.** A `return` leaves every statement still open
around it; a `throw` leaves every statement between itself and the handler.
Neither released what those statements had made, so

```absolute
foreach (Row row in source.all) {
    if (row.key != "") { return 1; }
}
```

leaked the array the loop walked and every string in it, once per call.
`break` and `continue` were always right, because both branch to a block
*inside* the loop statement and the release at its end still runs -- which is
why this was only visible with a `return` or a `throw` in the body. The
releases are emitted rather than popped off the list: a return is one path out
of the statement, and the paths that do reach its end still have to release
there. Pinned by `tests/early-exit-temporaries.abs`.

And one the same probes turned up on the other side of the model. **A
subscriber could be given a fresh allocation.** `weak T*` already refused it --
nothing would ever release what a weak name holds -- but `sub T*` says the same
thing about ownership and refused nothing, so

```absolute
class Tree { public sub Node* root; }
tree.root = new Node();
```

compiled, ran, and printed "memory leak detected for handle N" at exit: the
runtime's own check, on a program the analyzer had accepted. It is
`E_SUBSCRIBER_REQUIRES_EXISTING_OWNER` now, the same rule with the same wording
as the weak one.

An argument is the third place a value is bound to a name, and it refused
nothing for *either* qualifier: `takesSub(new Node())` and
`takesWeak(new Node())` both compiled and both leaked. The hook for it was
already there and empty -- `CheckManagedArgumentOwnership`, called from all six
places a call binds arguments -- so the rule is written once and reaches a
function, a method, a constructor, a base call and a function value.

A return is the fourth, and the subscriber form was not refused there either:
neither handing back a fresh allocation, which nobody then owns, nor handing
back a name for an owner that dies with the frame, which leaves the caller
naming storage that is already gone. The weak form had refused both all along;
they read alike now (`E_SUBSCRIBER_RETURN_LOCAL_OWNER`), and the two say which
of the two mistakes was made -- they shared one sentence until an indexer
getter written as a factory became the likeliest way to reach the rule.
Returning a *field*
stays legal -- its owner is the object rather than the frame, which is what
makes `sub T* p { get { return field; } }` the way a borrow is handed out.

`T* b = a;` is unchanged: an unqualified name takes a subscriber when what it
is given already has an owner and takes the owner when it does not, and the
rule asks which. See `tests/subscriber-fresh-owner-errors.abs`.

The shape of every fix is the same, and it is the shape the section above
describes: the rule already existed and the list of places it applied was
short. `EmitArrayElementRetain` is the mirror of `EmitArrayElementCleanup`;
`EvaluateIndexArguments` is the one place all four bracket forms evaluate an
index; `EmitCallableReturn` is what both bodies a callable can have end in;
`AccessorValue` is what all four ways of reaching a property or an indexer
produce. Each replaced two answers to one question with one.

Pinned by `tests/array-copy-parts.abs`,
`tests/indexer-argument-lifetime.abs`, `tests/closure-capture-lifetime.abs`,
`tests/closure-value-lifetime.abs`, `tests/array-accessor-owner.abs`,
`tests/accessor-owner-transfer.abs` and `tests/early-exit-temporaries.abs`, all
seven under AddressSanitizer with the leak check on, plus the `handing` shape
in `tools/testing/codegen_fuzz.py`.

### Fixed: a compound assignment never asked whether the operation exists

```absolute
string text = "a";
text += "b";      // was: Error: LLVM codegen: binary operator requires numeric operands
text = text + "b";  // E_NUMERIC_OPERAND_REQUIRED, with a file, a line and a column
```

`a += b` stores `a + b`, so `a + b` has to be an operation that exists. The
compound path asked only whether the value was assignable to the target -- and
a string is assignable to a string, a bool to a bool, a struct to a struct --
so every one of those passed the analyzer and failed in the backend, which
reported its own mechanism with no file and no line. The spelled-out form of
each has always been refused properly.

The compound form asks what the binary form asks now: numeric operands for the
arithmetic operators, integers for the bitwise ones. A raw pointer is still
exempt -- `p += n` steps by elements and has its own rule -- and a plugin
operator is left alone, because whether the compound form should reach a plugin
at all is a question about plugins rather than about this check.

**One more place an accessor is a call.** An owner produced by a property
getter and dropped inside the expression that read it -- `source.made.value` --
was never released; the flag that says an expression produced an owner was set
on the call path and cleared by hand on the property path. It is set the same
way on both now. The indexer half of it could not be answered and is the open
defect at the top of section 1. See `tests/accessor-owner-temporary.abs`.

**The ordering comparisons had the same hole from the other side.** `<`, `<=`,
`>` and `>=` were checked only for the operands being assignable to each
other, so `"a" < "b"` and `point < other` reached the backend and failed there
the same way. They order what the backend can order now -- numbers,
characters, enum members and raw addresses -- and inside a generic body the
question is recorded and answered at each instantiation, the way the ownership
rules already are, so `Sorted<Point>` names the line that cannot work.

Found while writing that: **a boolean was orderable by accident, and gave the
wrong answer.** One bit compared as a signed number reads `true` as -1, so
`false < true` came out false and `true < false` came out true. Two answers
were available -- compare it unsigned, or say a boolean is not an ordered
value -- and the second is what the language means. Equality is untouched.

Pinned from both sides: `tests/compound-assignment-operands.abs` is every
compound operator on the types that carry it, including the unsigned cases
where the two forms once picked different instructions, plus each ordering
comparison on each type it can order; `tests/compound-assignment-errors.abs`
is the refusals.

### Fixed: `sub T` did not survive a generic body, and neither did a move

Two compiler defects that only show together, and what they were blocking.

**A qualifier waiting for a type stopped waiting.** `sub T` is an instruction
with nothing to apply it to yet; substitution applies it -- `sub Node*` at
`T = Node*`, nothing at all at `T = int32`, because a value with no object has
no lifetime relation to weaken. A generic body is checked before any of that,
with every parameter substituted for itself, and the rule that drops the
qualifier on a non-handle dropped it there too. `sub T` became `T`, and `T`
does not take a `sub T` -- the arrow runs one way -- so a method of a generic
class could not be called with what another method of the same class had just
handed back (`no overload of 'weigh' accepts (sub T)`). Where the analyzer let
it through, at a constructor argument, the backend could not find the callee
and said so with no file and no line. Both halves substitute the same way now.
See `tests/open-qualifier-substitution.abs`.

**A move stopped being a transfer at the second hop.** A parameter of open
type `T` is role-polymorphic: the caller says whether it is handing over an
owner, with a flag beside the argument, and `move(v)` in the body checks that
flag at run time. The flag was set from "does this argument create a managed
owner", which inside an open generic body is always false -- `T` is not a
pointer yet. One hop worked, because the outermost caller passes a `new` and
the analyzer sees that. Two hops is a container wrapping a container, and there
the flag said "borrowed" about a value the caller had just given up:
`Ownership operation requires an owner argument`, then every element reported
as leaked. A move is a transfer whatever the parameter turns out to be, and
that is what the flag says now. See `tests/generic-move-handoff.abs`.

**What they were blocking.** Three classes were brought over to the ownership
model when it landed -- `Vector`, `VectorIterator`, and the container in
`tests/vector-owner-elements.abs` -- and the rest of `std/collections` was left
as it was: every one of them moved an element by reading a slot and storing
what it read, and a handle read out of a slot is a second handle to one object.
So no standard container could be instantiated over an owning element type at
all. Naming `Deque<Cell*>` was a compile error inside the library; no line had
to use it.

`Deque` is now written the way the model requires, and with it `Queue` and
`Stack`: growth carries the elements rather than copying them, a pop takes its
slot rather than reading it, `clear` releases the live run -- one run or two,
because it is a ring -- and a read that is not the deque giving the element up
hands back `sub T`. `tests/deque-owner-elements.abs` counts every destruction
and runs under AddressSanitizer with the leak check on.

`PriorityQueue` follows, and it is the harder shape for a different reason: a
heap swaps on nearly every insertion and removal, so `swap` is the line the
whole container turns on -- three reads there gave two slots one handle each
time. `enqueueAll` is the other half: adding a batch by reading each slot left
the array holding what the queue now had, and the runtime said so. The batch is
handed over element by element now, and a take clears only what has something
to clear, so at `T = int32` the caller's array is untouched. See
`tests/priority-queue-owner-elements.abs`.

`Vector` follows too, and it is the one the indexer projection unblocked. Its
own body was already right -- it moves elements rather than copying them, and
`first`, `last` and `toArray` hand back the subscriber form -- but
`Vector<Cell*>` could not be instantiated at all, because a **method's return
type is instantiated with the class** and `builder()` returned a
`VectorBuilder<T>`. A program paid for a builder it never called. `builder()`
is an extension now, instantiated where it is called, so a vector of owners is
an ordinary vector and only a program that asks one for a builder is refused --
with the lines that say why. `tests/std-vector-owner-elements.abs` runs one:
push, read, write, swap, take, pop, remove, iterate, and the destructor count
at the end.

`swap` and `takeAt` are what the container gained. Both are the operations the
projection made necessary to name: `v[i]` borrows the cell, so rearranging is
an exchange between slots the vector already has, and giving an element up is
`takeAt`. The generic algorithms over a vector are written with them --
`sort` and `reverse` swap rather than holding a key out in a local -- and the
ones that only read take `sub T`: a comparator, a predicate, a mapper, a search
target. `filter` returns `Vector<sub T>`, because a filter selects and what it
selects still belongs to the source; at `T = int32` every one of those
qualifiers is absent, which is why `tests/collection-algorithms.abs` is
unchanged. `tests/std-algorithms-owner-elements.abs` runs the same algorithms
at an element type that owns.

`Set` and `Map` follow, and neither had been migrated at all. Both moved
elements by reading a slot and storing what they read, and both kept a
copy-on-write flag whose only writer was `iterate()` -- an iterator already
takes its own snapshot, so the flag bought nothing and its `copy` was the one
operation an owning element refuses. Both are written the way the model
requires now, both took the same `builder()` move out of the class, and
`tests/std-set-map-owner-elements.abs` counts every destruction under
AddressSanitizer.

**A map is where a recorded claim turned out to be wrong**, and the correction
matters more than the container. This file said that a map's element is a
struct, that `sub` weakens a handle and a struct is not one, and that there was
therefore no way to say "this array names what the map holds". The second half
does not follow from the first: the pair is *generic in its value*, so the
snapshot does not weaken the struct -- it instantiates it at the weakened
parameter. `KeyValuePair<K, sub V>` is a pair whose value half borrows, and at
`V = int32` it is the pair it has always been. `MapIterator`, `entryAt` and
`toArray` are written with it. The two instantiations are two types, so the
snapshot is filled field by field rather than by `copy`: it is the per-field
store that weakens, and copying the array wholesale gave the snapshot the map's
values, so deleting the iterator destroyed them.

`HashMap` and `HashSet` are the last containers, and the same shape answers
them: the iterator's `HashKeyValuePair<K, sub V>` is the pair instantiated at
the weakened parameter, exactly as the map's is. A hash table has two places
the ordinary containers do not, and both were wrong for an owning element:

- **Rehashing.** Growth copied the old table and read each live slot into the
  new one, so both tables held a handle to every element -- and the old table
  is dropped as the call returns. Every live entry is taken out of the old
  table before the table is replaced now, which leaves exactly one name for
  each entry at every moment in between.
- **Tombstones.** `remove` marked the slot dead and left the handle in it.
  Nothing looks at a tombstone again, so that was a leak for every entry a
  program removed; the slot gives back what it held.

`HashSet`'s element is its key, so its hash and equality callbacks take `sub T`
-- they read an element and keep neither. At `T = int32` `sub T` is `T`, and a
caller writes them as before. `tests/std-hash-owner-elements.abs` counts every
destruction, and its insert loop is long enough to force several rehashes.

Two defects in the compiler surfaced on the way, both of them a qualifier
carried where it does not belong:

- **A `move` was not an owner inside an open generic body.** The rule that a
  field or a slot must be filled from a fresh owner cannot run where `T` is not
  a pointer yet, so it records the question and each instantiation asks it --
  but it recorded `value = move(v)` too, because `createsManagedOwner` is false
  for a `move` of an open `T` exactly as it is for reading a slot. A `move` is
  the operation that hands an owner over and is refused on anything that is not
  one, so its result is an owner whatever `T` becomes; every instantiation over
  an owning type was reporting a field the body had taken correctly.
- **A generic parameter was bound to a qualified form of itself.** Calling
  `put(borrowed)` inside a container's own body unified the parameter `V`
  against the argument type `sub V` and produced a *specialization of the
  method* with `sub V` where `V` was written. Every ownership rule that then
  asked what the parameter takes was told "a borrow" about a parameter declared
  to take, so handing a borrow straight to a taking parameter was accepted in
  silence. `sub V` is not a type `V` could be -- it is the borrow of whatever
  `V` turns out to be -- and inside an open body `V` is already bound. Only the
  same name is refused: at `take<T>(T v)` called with a `sub Cell*`, the base
  name is `Cell*` and `T` still takes it.

What is left is one decided question and one undecided one.

**Decided: a builder is a snapshot, and a snapshot is for elements that can be
copied.** `VectorBuilder`, `SetBuilder` and `MapBuilder` are staging owners
seeded from a live container's storage, so over an owning element type the
first act of each is to duplicate what that container still holds; borrowing
instead does not help, because `finish()` would then hand a container of owners
a set of handles it never owned. That is not a gap to close -- it is what the
operation means. A modified copy of an element that owns requires duplicating
it, and this language deliberately has no generic clone.

So the three of them say it: `class VectorBuilder<copyable T>`. A generic type
may state what it requires of a parameter, written where every other qualifier
in this language is written -- in front of the name -- and the requirement is
checked where the type is *used*. There is one requirement, because the
ownership model answers exactly one question of this shape: `copyable`, whether
a second name for a value is an ordinary thing to have. A number, a string, a
borrow and an observer all answer yes; an owner and an aggregate holding one
answer no.

What this buys is the diagnostic. Asking `Vector<Cell*>` for a builder produced
six errors, `Set<Cell*>` seven and `Map<K, Cell*>` two, every one of them naming
a private field of a class the program never wrote, and not one of them on the
line that asked. It is one sentence now, on that line. An instantiation a
requirement refuses does not replay its body's facts at all: the requirement
said the one thing worth saying, and the body would say it again about storage
the program cannot see. See `tests/generic-constraints.abs`,
`tests/generic-constraints-errors.abs`, and
`tests/std-collections-owner-elements-errors.abs`, which pins that the builders
say it once.

**And the other operation does not need a name**, which was worth finding out
by trying to give it one. A builder that *took* its elements rather than
copying them would be sound for every `T`, so it looked like the missing half.
It is not: a builder exists for the snapshot -- staging changes while the
original stays valid -- and an operation that empties the source has already
given that up. What is left of it is "move these elements somewhere I can
change, then use that", and over a container whose elements move correctly
that is the container:

```absolute
Vector<Cell*>* staged = new Vector<Cell*>();
while (source.count > 0) { staged.push(source.takeAt(0)); }
staged.removeAt(0);
staged.push(new Cell(42));
```

Nothing is duplicated, nothing is released by the transfer, and `staged` is the
published result -- there is no `finish()` to call because there is nothing
left to publish it *from*. `tests/std-vector-owner-elements.abs` runs exactly
that and counts it. So `intoBuilder` would be a second name for `Vector`, and
it is not there.

The advice a program gets when a builder refuses it should say this rather than
the general form, and today it says the general form: "borrow it with 'sub', or
hand it over with move(...)". That is true and it is not the sentence someone
holding a `Vector<Cell*>` needs.

**When a builder would earn its place**, so that this is not re-argued from
scratch the next time someone reaches for one. There are three cases, and none
of them is "staging changes to a mutable container":

1. **A bridge to a frozen type.** When the published object is constant after
   `finish()` -- a `FrozenHashMap` behind a symbol table, an immutable set --
   the builder is the only thing that can be mutated, and `finish()` is what
   freezes. There is nothing else to write the contents with.
2. **An invariant that step-by-step insertion cannot hold.** When the finished
   structure is built by one expensive pass over the whole batch rather than
   one insertion at a time -- a perfect hash, a sorted and deduplicated run
   laid out as a flat B-tree -- `finish()` is that pass, and the builder is
   where the batch accumulates until it can run.
3. **A model without an honest move.** In a language where transferring
   ownership is not O(1), a staging layer is what keeps the copies down. This
   one does not apply here, and it is listed so it is not mistaken for one
   that does.

   Be exact about what is known, because the test that looks like it settles
   this does not. `tests/std-vector-owner-elements.abs` counts destructor
   calls: it establishes that a transfer duplicates nothing and releases
   nothing, and it would catch a release too many, a release too few, or two
   names for one element. It does not measure cost. That the transfer is also
   O(1) is a property of what the backend emits, and a change that made it
   O(n) while keeping ownership balanced would leave those counts unchanged.
   Guarding the cost needs a measurement, not a counter.

`std/collections` has none of the three: its containers are mutable, an element
moves without copying, and nothing it publishes is frozen. That is why
`builder()` is a snapshot and nothing else, and why the operation that takes
instead of copying is the container. If a frozen structure is ever added, the
first case applies and a builder returns -- not as a wrapper over `Vector`, but
as the only way to construct something that cannot otherwise be constructed.

### Fixed: a write through a reference did not overwrite

```absolute
void twice(int32 i, out string result) {
    result = format("a-{}", i);
    result = format("b-{}", i);   // the first string was never released
}
```

`out T` and `T&` name a slot the caller owns. The callee releases nothing when
the call ends -- the slot is not its -- and that much was right. But a write
through the name is still a write to that slot, and a slot gives back what it
held: the same rule a local, a field and an array element already follow. The
identical assignments to a local were correct all along, which is what made
this quiet; the leak is per write rather than per program, so a function that
fills an `out` parameter in a loop leaked one value per iteration.

The two halves are separate now. Owning the slot decides what happens at the
end of the call; naming it decides what happens on a write. A first write into
an empty slot is unaffected: an uninitialized local is zeroed, and releasing
nothing is nothing.

Pinned by `tests/reference-slot-overwrite.abs`, under AddressSanitizer with the
leak check on, because the values are strings and nothing there can assert its
own release.

**A module-scope name is a place too**, and it was the other name for the same
mistake: `label = format(...)` in a loop leaked every value but the last. The
identical line on a *static field* was already right, because a static field is
reached as a field and takes the field rule, while a module-scope name is
reached as a variable and took a branch that asks whether the name owns its
slot at the end of a scope. For a global the answer to that is no -- it
outlives every scope, nothing walks it at exit, and that is why a module-scope
owner is refused outright. What is left is the write. See
`tests/module-scope-slot.abs`.

### Fixed: an aggregate parameter borrowed, and the exception cost what exceptions cost

```absolute
void writeField(Row row, int32 index) {
    row.key = format("f-{}", index);   // released the *caller's* string
}

int32 rewrite(Row row, int32 index) {
    row = rowOf(index);                // and this one nobody released
    return ...;
}
```

A by-value parameter is a copy, and a copy counts the parts it now names. A
string parameter was given that answer long ago -- the caller hands over a
count and the parameter gives it back at the end of the call -- and an
aggregate parameter was made an exception: it borrowed.

Two failures, one cause. Writing a *field* of the parameter took the field
rule, which releases what the field held -- and what it held was the caller's,
so the caller was left naming freed bytes. Writing the *whole* parameter could
not release anything, because the same reasoning says the old value is the
caller's, and the new value then had no owner at all and leaked. Per write,
not per program: a callee that fills a parameter in a loop leaked one value an
iteration.

A parameter that is only read was unaffected, which is why this went
unnoticed -- reading is what parameters mostly do, and the whole suite reads
them.

Owning from the start makes both writes ordinary. A reference parameter stays
excluded: it names the caller's slot rather than a copy of it, so it has
nothing of its own to count, and the entry above is that half. A struct of
plain values counts nothing and is unchanged, because the walk asks the type.

Pinned by `tests/parameter-copy-counts.abs`, which fails as a use-after-free
rather than a leak when the fix is removed.

### Fixed: a type name was matched by prefix, and the compiler segfaulted

```absolute
int32[] flat = {1, 2};
int32[][] rows = flat;    // no diagnostic; the compiler crashed
```

`IsNumeric` asked whether a type name *starts with* `"int"` or `"uint"`. Eight
names is the whole set and `int` and `uint` on their own are not types, so the
prefix looked safe. It is not: `"int32[]"` starts with `"int"`, and an array of
integers was a number everywhere the analyzer asked.

One root cause, nine wrong answers. Arrays could be added, subtracted,
compared, masked, used as a condition, printed, formatted, assigned to a scalar
and assigned from one. Each either reached the backend and failed there naming
the backend's own mechanism with no line -- "value cannot be used as a
condition", "binary operator requires numeric operands" -- or did something
worse. The worst was the rank mismatch above: nothing refused it, and the
backend read a dimension the descriptor does not have, so **the compiler
segfaulted with no diagnostic of any kind**. `IsInteger`, `IsConditionType` and
`IsPrintableType` read the same prefix; all four name the eight types now.

The rank rule that came out of fixing it is worth stating on its own. An array
may be seen at a **lower** rank than it has, because the storage is row-major
and contiguous -- that is what lets the grouped literal `{{1, 2, 3}, {4, 5, 6}}`
fill an `int32[]` of six, which `tests/array-advanced.abs` has always covered
and which had been passing through the same hole. It may never be seen at a
higher one. Before, `IsAssignable` compared array types by stripping one
bracket from each and asking again, so rank never entered into it.

The refusals are `tests/numeric-type-names-errors.abs`, all fourteen
diagnostics with a file, a line and a code.

### Fixed: an interface by value was refused in five places and not in four

An interface is a dispatch table, not a value: it has no size and no storage of
its own, so it is used through a pointer. A declaration said so -- `I one;` is
`E_INTERFACE_REQUIRES_POINTER`, and so is a field of one, a struct member, and
a return type. Four other places a type is used as a value did not, and each
reached the backend:

```
Error: LLVM codegen: unsupported type 'I' (ctx='DeclareFunction:main', ...)
```

its own mechanism, no file, no line. They were an array's element type, a
parameter, a tuple element, and a generic argument.

The first three ask one question in one place now. The fourth is answered at
each instantiation instead, from what the body did with the parameter, the way
the ownership rules already are -- because the body is what decides. `Box<T>`
holding a `T` by value cannot take an interface; `Viewer<T>` holding
`sub T*` is exactly how an interface *should* be kept, and refusing
`Viewer<I>` would have refused the fix. Judging it from the argument alone
gets that wrong, which is what the first attempt did.

See `tests/interface-value-type-errors.abs`.

### Fixed: the size of an allocated array was evaluated and then ignored

```absolute
int32[] a = new int32["hello"];
println(format("len={}", a.length));   // len=-1250607064
```

Two of the three places an array size is written asked whether it is a number:
a declarator (`int32 fixed[n]`) and an array literal. The third -- `new T[n]`,
which is the one people write -- evaluated the size against an expected `int64`
and threw the answer away. No diagnostic, no crash, and a length read out of
whatever the pointer happened to be, with every index into it then
bounds-checked against garbage. Only the first dimension was evaluated at all,
so the rest were unexamined too.

Zero stays legal there, and that is the difference from the declarator form:
`new int32[0]` is an array with nothing to read, which
`tests/array-zero-initialization.abs` relies on, while a declarator's storage is
the frame's and has to have a size. Copying the declarator's rule wholesale was
the first attempt and it broke both of those tests, which is how the
distinction got written down.

See `tests/array-allocation-size-errors.abs`.

## 2. Missing features that fail loudly

All of them are closed. Four were notations that did not lex; the fifth was a
default parameter value, which parsed and was then ignored. None of them was a
wrong answer -- each failed at the point of use, loudly -- but they are all
ordinary notation, and a language with `double` and no way to write `1e-9` is
missing something people reach for immediately.

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

### Fixed: a default parameter value was parsed and then ignored

```absolute
int32 twice(int32 v = 3) { return v * 2; }
twice();       // was E_NO_MATCHING_OVERLOAD: no overload of 'twice' accepts ()
```

The default was parsed, stored on the parameter, and never used: nothing filled
it in at a call site, so a call that omitted the argument was refused as having
no matching overload -- which named the wrong thing, because the overload the
author wrote was right there. The standard library declares seven of them, so
`new Deque<string>()` did not compile although its own signature said it
should.

**A default is a constant**, the same restriction a static field's initializer
already carries. That is what makes filling it in at the call site mean the
same thing as filling it in at the declaration -- there is nothing in it that
could read the callee's frame -- and it is what bounded the change: every
default in the standard library is a literal. It must also be trailing, because
a call fills the missing arguments in from the end.

Three pieces:

| | |
|---|---|
| the declaration | records what to put there, in the collect phase, because a call may be analyzed before the declaration's body is |
| overload matching | accepts an arity down to the last required parameter, and charges a point per argument the call did not write, so an exact overload still wins |
| the call | appends the missing expressions before evaluating them, in the one place that already builds an argument list |

A constructor call reaches its callee differently from a function or a method:
it has no name to resolve, so the analyzer now records which constructor was
selected on the call's own `ExpressionInfo`, and the backend fills in from
there.

Pinned by `tests/default-arguments.abs` -- a function, a method, a constructor,
a method of a generic type, two defaults in a row, an exact overload beating a
filled-in one, and a container from the standard library built the way its
signature says -- and by `tests/default-arguments-errors.abs`, which is the
non-constant and the non-trailing default.

## 2a. Fixed: one name over two questions in `TypeSemantics`

`TypeSemantics` is shared between the analyzer and the backend, and
`type_names.h` says so: one place decides what a type is, for every question
about copying, moving and releasing it. Its `needsDrop` is read by both halves
and means two different things.

```absolute
struct WithString { public string t; }
WithString copied = original;      // allowed, and rightly
```

The backend's `needsDrop` is *there is something to release*, and for
`WithString` it is true -- the string field has a count to give back. The
analyzer's is *this owns a unique resource*, and for `WithString` it is false --
which is what lets the copy above be a copy rather than
`E_RESOURCE_AGGREGATE_COPY`. Both answers are correct for the rules that read
them, and every rule reading one of them today reads only its own half, so
nothing is wrong at the moment.

What is wrong is the name. A field claiming to be the one answer while holding
two is exactly the shape of the defects in section 1: a reader who checks the
struct's definition learns the wrong thing about one of the halves, and the
next rule written against it picks whichever meaning its author had in mind.

Settled the second of the two ways it could be: `needsDrop` means "there is
something to release" on both sides now -- the analyzer answers yes for a
string, for a closure, for a tuple holding either, and for any aggregate whose
parts do -- and the ownership rules ask
`Analyzer::TypeOwnsUniqueResource`, which is the question they were always
asking. Every rule that used to read `TypeOwnsResources` reads that instead,
and none of them changed behaviour: a struct holding a string is still copied,
a struct holding an owner is still refused, `copy` of a `string[]` still works
and `copy` of a `Node*[]` is still refused.

One honest consequence: no rule inside the analyzer reads `needsDrop` today,
because every rule that seemed to was asking the other question. The field is
there because the model is shared and the backend reads it, and it is now
*right* rather than differently defined. What keeps it from drifting again is
that the question it answers is written next to it in `type_names.h`, and the
other question has a name of its own instead of borrowing this one.

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
- **Defer, exceptions and what they leave behind: clean.** A deferred body
  reads its variables when it runs and not when it is written; one registered
  inside a loop runs once per iteration, including on the iteration that
  `break`s; `finally` runs exactly once through a `return`; a rethrow, a throw
  from inside a `catch`, and an unmatched inner handler each reach the right
  outer one. Under AddressSanitizer at `-O0`, an owner alive when an exception
  passes through -- in a scope, in a loop, in the middle of an expression, in a
  constructor that threw after allocating a field, in a property setter that
  refused its value -- is released exactly once in every case.
- **Dispatch: clean.** An override is found through a base-typed name, through
  an interface-typed name, and from inside an interface's own default body.
- **`match`: clean at the widths.** `2^31`, `2^32`, `int64`'s maximum, `int32`'s
  minimum and `uint32`'s maximum all select their own arm and nothing else.
- **`std.binary`: clean.** Every writer/reader pair round-trips, including an
  empty string, a multibyte one, `int64`'s minimum and a byte of 255; a read
  past the end returns zero without moving the position.
- **`std.encoding`: clean.** Base64 padding, non-canonical trailing bits, hex
  with an odd digit count, and UTF-8's overlong, surrogate and truncated forms
  are all refused where they should be.
- **`std.uuid`: clean.** The canonical form parses in either hex case and
  refuses a misplaced dash, a short string and a non-hex digit; a generated
  value is version 4 with the RFC variant bits, and it survives a round trip
  through its own text.
- **`std.fs` path helpers: swept and now covered.** Five of them answered
  differently on the two targets; all fixed, at `tests/path-lexical-edges.abs`.
  The rest -- `join` with an absolute or empty side, `stem` and `extensionOf`
  on a dotfile and on a dot inside a directory name, `resolve`, `isAbsolute` --
  were right and agreed.

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
   documented boundary rather than a defect (docs/generics.md). There is one
   constraint now -- `copyable`, in section 1 -- and it is deliberately not a
   general one: it names the single question the ownership model already
   answers. An arithmetic constraint would be a second question, and nothing
   has needed it. See
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
   Two more disagreements came out later, and they are worth reading as a limit
   on this sweep rather than a gap in it: the two targets parsed ISO-8601
   timestamps differently (section 20), and they still print real numbers
   differently -- one of them not at all (section 22). Every test in the corpus
   agreed anyway, because none of them wrote down an input where the two
   implementations differ. Building the same corpus twice proves the corpus,
   not the target. What finds these is asking the two copies the same question
   directly, which is what section 22 did.
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
kind of leak. Everything that remained traced to `absolute_string_*`, which was
the open defect in section 1 when this was written and is closed now: a string
has a lifetime.

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

### Three more, for values whose lifetime is counted

The shapes above look at what a program computes. Three more were added for
what it *holds*, and they are where the ownership entries in section 1 came
from:

| Shape | What it carries, and where the others do not go |
|---|---|
| `counted` | a struct of strings and a tuple through every place a copy happens -- `aggregates` builds tuples of numbers, `text` builds strings with no aggregate around them |
| `owned` | a struct holding `Node*`, `sub Node*`, `weak Node*` -- assigned, returned, put in a slot, borrowed by a parameter -- where `handles` owns objects but never puts one in a struct |
| `flowing` | counted values crossing statement boundaries: an early exit, an unwind, a deferred body, a branch arm, a loop header that made what it iterates over |

Each found something on its first long run, which is the useful measurement:
`counted` found both array initializers storing without counting, `owned` found
assignment overwriting a resource-owning aggregate without destroying it, and
`flowing` found a temporary array releasing its storage and nothing in it.

What they share is a discipline the earlier shapes did not need. A leak of a
counted value is only visible on the *second* pass over the same container --
the first pass leaves the count one too high and everything still reads -- so
every loop in these three runs twice over the same data. And a checksum is
built from code points rather than from a length, because a count that is one
too low frees the bytes and reads what the allocator put there next, which is
often the right length and the wrong text.

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

## 20. Beyond the list: text that names a time, and text that names a place

The sweeps so far went after the language. This one went after the two places
where the standard library turns a *string* into a value with a meaning --
`std.datetime.parseIso` and `std.uri` -- because the language's own defects
announce themselves and these do not. Nothing here crashes, nothing leaks, and
every one of them answers.

Eight defects and one of their consequences, all of the kind this file was
started for: the program keeps running and the answer is wrong. The seventh
entry below is the sixth one seen from another module, and it is kept separate
because that is where a reader would look for it.

### Fixed: a fraction of a second was read as a count of milliseconds

```absolute
std.datetime.parseIso("2024-01-01T00:00:00.1Z").time.millisecond   // was 1
```

`.1` is a tenth of a second. It was read as one millisecond, `.12` as twelve,
and `.1234` was refused outright as "invalid millisecond: 1234" -- a message
about a field the text does not contain. The runtime read the fraction with
`sscanf("%d")`, which is a parse of an integer and cannot know how many digits
it consumed, so only a fraction written with exactly three digits was right.
Every timestamp a machine writes has three; every timestamp a human writes
does not.

A fraction is now scaled by the digits it is written with, and digits past the
third are truncated, because a millisecond is the resolution `Time` has. `.1`
is 100 ms, `.000999` is 0 ms rather than 999, and `,5` -- the other decimal
mark ISO-8601 allows -- is 500 ms.

### Fixed: a bare date read its own day as a time zone

```absolute
std.datetime.parseIso("1999-12-31")   // was 1999-12-31T00:00:00-31:00
```

Thirty-one hours is not a time zone; no zone on Earth is past ±14:00. The zone
scan looked for the last `+` or `-` after the `T`, and when there was no `T` it
looked from the start of the string, where the last `-` is the one in front of
the day. So a calendar date became a datetime in a zone that does not exist,
and the instant it named was a day and seven hours off. `2024-06-15` was
`-15:00`; the further into the month, the further the error.

This and the fraction were one mistake: the parser found its fields by
scanning for punctuation rather than by reading the grammar, so neither field
knew where it ended. It reads the grammar now --
year, `-`, month, `-`, day, and only then an optional time and an optional zone
-- so the date ends where it ends and there is nothing left for a zone to be
read out of. A bare date is UTC at midnight.

Reading it as a grammar refuses things the scan accepted by not looking:
`2024-1-1` (a month that is not two digits), `2024-01-01T00:00:00+99:00` (an
hour that is not an hour), and trailing text after a value that parsed. What
was accepted before and still is: a space in place of the `T`, a missing
seconds field, `Z` in either case, `+0530` as well as `+05:30`, and an expanded
year longer than four digits.

### Fixed: a second before the epoch rounded the wrong way

```absolute
std.datetime.toUnixSeconds(instantAt(-500))   // was 0, is -1
```

`toUnixSeconds` divided milliseconds by 1000, and integer division truncates
toward zero -- which is the wrong direction on the left of the epoch.
1969-12-31T23:59:59.500Z came back as second 0, which is 1970-01-01T00:00:00Z:
half a second in the past became half a second in the future, across the one
boundary a Unix time has. The round trip through `fromUnixSeconds` landed a
whole second late and on the other side of it.

It floors now. After the epoch nothing changes, which is why this survived: the
only values that show it are the ones nobody tests with.

### Fixed: a negative year lost a digit on the way out

`formatIso` printed a year with `"%04d"`, which spends one of its four
positions on the sign, so year -1 came out as `-001-01-01T00:00:00Z` -- three
digits, and not something `parseIso` reads back as year -1. The sign is now
printed separately from the four digits, and `formatIso` -> `parseIso` is the
identity again.

### Fixed: native and wasm did not agree on what a timestamp is

The wasm shim does not call the C++ runtime; `absolute_wasm_std.c` has its own
`absolute_datetime_parse_iso`, and the two were never the same parser. The
shim required a string of at least 19 characters with `T` at index 10 and a
zone written as `+hh:mm`, which rejected a bare date, a value with no seconds,
a space in place of the `T`, `+0530` and `+05` -- all of which the native
parser accepted. It also got the fraction *right* while native got it wrong, so
`.1` was 100 ms on wasm and 1 ms natively: the same program, the same input,
two answers.

Section 5's WASM sweep built every test in `tests/` for both backends and
diffed them, and this survived it, because agreement is only checked on the
shapes some test writes down and no test wrote down a fraction that was not
three digits. `tests/datetime-iso-edges.abs` writes them all down now, and it
is an ordinary member of the corpus, so `tools/testing/suite_differential.py`
compares its answers on both targets. Porting the fixed grammar into the shim
was how this entry got closed -- the differential reported it as a mismatch
first, which is the check doing exactly what section 19 says it is for.

The shim also printed a negative year with `"%04d"`, the same way, and now does
not.

### Fixed: percent-encoding was a list of four characters

```absolute
std.uri.decode(std.uri.encode("100%20"))   // was "100 "
```

`std.uri.encode` replaced a space, a quote, `<` and `>`, in that order, and
`decode` replaced the four escapes back. Everything else passed through --
including `%` itself, which is the one character an escape mechanism must
escape. So encoding was not injective and decoding was not its inverse: text
that already contained a percent sign came back as something else, and text
containing `&`, `=`, `?`, `#` or `/` was handed to a URL unescaped, where those
characters mean something.

Both are RFC 3986 now, over UTF-8 octets: everything outside the unreserved set
(`A-Z a-z 0-9 - . _ ~`) is `%XX`, and `decode` reverses any valid escape in
either hex case. A `%` that does not begin an escape stays literal, because a
decoder that throws on `100%` cannot be used on input from a network.

### Fixed: and so a form body was never decoded

`std.form.parseUrlEncoded` splits a body on `&` and `=` and then calls
`std.uri.decode` on each half -- which is the right order and the right call,
and it did nothing, because of the entry above. `email=user%40example.com` came
back as the string `user%40example.com`. Every form value carrying an at sign,
an ampersand or a percent sign was wrong, and wrong in a way that reads as the
user's mistake rather than the parser's.

It was reached by fixing `encode`, not by testing `std.form`, which is the
lesson section 23 already records: after a fix, ask who else calls it.

### Fixed: an authority is not "everything before the first colon"

```absolute
std.uri.parse("http://user:pass@example.com/x").host   // was "user"
std.uri.parse("http://[::1]:8080/x").host              // was "["
```

The port was taken from the first colon in the authority. Userinfo has one, and
an IPv6 literal is made of them. So `user:pass@example.com` parsed as host
`user` on port `parseInt("pass@example.com")`, which is 0 -- and `std.http`
hands exactly those two to `headers.put("Host", uri.host)` and
`connectWithRetry(uri.host, uri.port, ...)`. An IPv6 literal parsed as host
`[`, on port 0 as well.

Userinfo is dropped at the last `@`, a bracketed host is read to its `]`, and
only the colon after the host introduces a port. Anything else after the host
is refused where it is written rather than turned into a zero.

### Fixed: a port was dropped because some other scheme implies it

`Uri.toString` omitted the port whenever it was 80 *or* 443, whatever the
scheme. So `http://example.com:443/x` printed as `http://example.com/x`, which
is port 80 -- a different endpoint, produced by a round trip through a type
whose whole job is to survive one. `https://example.com:80/x` lost its port the
same way.

A port is now omitted only when it is the one its own scheme implies, and
`defaultPortFor` is the single place that says which that is -- it was already
being decided twice, once in `parse` and once in `toString`, and the two
disagreed.

`tests/datetime-iso-edges.abs` and `tests/uri-percent-encoding.abs` pin all of
them, and both are ordinary members of the corpus, so the suite differential
runs them at two optimization levels and on both targets. At the boundaries
that show them: the fraction at one, two, three, four
and six digits, the instant at -1500, -1000, -500 and +500 ms, the round trip
through text that already looks encoded, and the authority as a bare host, a
host with a port, one behind userinfo, and an IPv6 literal with and without a
port.

## 21. A leak probe that proves nothing

A probe can pass for the wrong reason (section 23), and there is a second way
for it to do that which costs more than a wrong assertion: the probe can be
deleted before it runs.

`--build-exe` optimizes. LLVM removes an allocation whose result no program
point observes, and a leak probe is exactly that shape -- allocate, do not
free, exit. This one reports nothing:

```absolute
extern "C" raw int8* malloc(int64 size);
void leakOne() { raw int8* p = malloc(64); *p = 1; }
```

A hundred calls, 6400 bytes never freed, and LeakSanitizer is silent: the
`malloc` is not in the binary. Built with `-O0` the same program reports all
6400 bytes. The same erasure hides a use-after-free and a double free written
against `malloc` and `free` directly -- the pair is removed together, so the
program prints "survived double free" under a sanitizer that would certainly
have caught it.

So: **build ownership and leak probes at `-O0`**, or reach the allocator
through something the optimizer cannot reason about. The suite's own sanitizer
cases are not affected, and it is worth saying why, because it is not luck.
`sanitizer-uaf` and `sanitizer-double-free` send their pointer through
`native_pointer_alias`, an external C call, so nothing about the allocation is
removable and AddressSanitizer reports both. `sanitizer-leak` does not use
LeakSanitizer at all: it allocates through `absolute_managed_create` and
asserts on the runtime's own report, `memory.leak.detected`. A probe written by
hand against libc gets neither protection.

LeakSanitizer has one more way to stay quiet that is worth knowing: it reports
only what is *unreachable*, so a leaked pointer still sitting in a live stack
slot at exit is not a leak to it. `malloc` in `main` with the pointer never
overwritten is clean by construction.

## 22. Beyond the list: one thing said twice, and the two answers

Section 20 fixed an ISO-8601 parser that native and wasm had each implemented
separately, and the entry noted why the WASM sweep in section 5 had not caught
it. That is a shape, not an accident, so this sweep went looking for it: **every
place the runtime is written twice** -- once in C++ for the host and once in
`Absolute-Runtime/wasm/` for the shim -- and asked the two copies the same
question.

Nine defects. In seven of them the two copies disagree, and in five of those
the wasm copy is the correct one, which is worth saying plainly: the shim is
not a reduced version of the runtime, it is a second implementation, and it is
sometimes the better one.

The one thing this sweep found and did not fix is at the end, and it is the
largest: **there is no routine that turns a real number into text.** Native
borrows the C library's, and wasm has none at all.

### Fixed: a JSON document could contain a byte no parser accepts

```absolute
std.json.parse("{\"x\":\"a\\u0001b\"}").stringify()   // was a raw 0x01 byte
```

RFC 8259 does not allow an unescaped code point below U+0020 inside a string.
The native writer escaped `"`, `\`, `\n`, `\r` and `\t` and passed everything
else through as itself, so a document that arrived carrying U+0001 left with
that byte unescaped. What it produced was not JSON, and nothing but this
library would read it back.

It now writes `\b`, `\f` and `\u00XX` as well -- which is what the wasm writer
already did. The native *parser* had the mirror of the same hole: it accepted a
raw control byte where wasm refused one, so the two targets disagreed about
whether a document was legal. Both refuse it now.

### Fixed: an emoji came apart in the middle

```absolute
std.json.parse("{\"x\":\"\\uD83D\\uDE00\"}")   // was six bytes, two "characters"
```

A character outside the basic plane is written in JSON as a UTF-16 surrogate
pair, and the two halves are one character. The native parser encoded each half
on its own, producing six bytes of CESU-8 -- invalid UTF-8, inside a type whose
whole contract is that it holds UTF-8. Every emoji, and every character above
U+FFFF, in every document, on the native target only: wasm combined the pair
correctly and produced the four bytes.

Section 4 records "Strings and text: 23 of 23 clean" including emoji, and that
is still true. The defect was never in the string library; it was in a second
UTF-8 encoder written inside the JSON parser, which nothing had asked about.

Native combines the pair now, and both targets refuse half a pair on its own,
which is the only other thing it could be.

### Fixed: `\q` was an escape that decoded to `q`

The native parser's `default:` arm appended whatever character followed the
backslash. So `"a\qb"` parsed to `aqb` -- a string the document does not
contain, invented rather than refused. wasm rejected it. JSON has eight
escapes; anything else is now an error on both.

### Fixed: writing a number that is not a number

`0.0 / 0.0` and `1.0 / 0.0` stored in a JSON document came out as `nan` and
`inf`. JSON has neither, so this library wrote a document that this library's
own parser then refused -- the shortest possible round trip, and it failed.
Both targets write `null` now, which is what JavaScript's `JSON.stringify`
does with the same values.

### Fixed: a cast that was undefined, and what it actually produced

Both writers decided "is this a whole number?" with `(int64_t)d == d`. The
conversion is undefined when the value does not fit an int64, which is not a
theoretical objection -- on wasm, `1e19` really did convert to `INT64_MIN`:

```absolute
std.json.parse("{\"x\":1e19}").stringify()    // was {"x":-9223372036854775808.}
std.json.parse("{\"x\":1e-7}").stringify()    // was {"x":0.}
```

Both of those are the wrong number *and* invalid JSON -- the trailing-zero trim
left a bare `.` -- and the parser refused its own output. The range is now
tested before the cast, on both targets. Section 19 read the emitted IR for
undefined behaviour and found the language clean; this is the same question
asked of the runtime, where the answer was different.

### Fixed: five path helpers, five disagreements, five times wasm was right

`std.fs`'s lexical helpers touch no filesystem, and native and wasm answered
differently for all of these:

| | native | wasm |
|---|---|---|
| `normalize("a/b/..")` | `a/` | `a` |
| `normalize("a/")` | `a/` | `a` |
| `join("a", "b/..")` | `a/` | `a` |
| `parent("/a/")` | `/a` | `/` |
| `fileName("/a/")` | empty | `a` |

The native ones are C++'s `lexically_normal()` faithfully applied, and its
answer is that a trailing separator survives -- so `normalize("a/b/..")` and
`normalize("a")` were two different strings for one path, and normalizing was
not worth doing. The wasm virtual filesystem strips the trailing separator,
which also makes `parent` and `fileName` agree with `basename` and `dirname`,
and with every other language's path type.

A trailing separator is not a component. One `CanonicalPath` says so once, and
the six entry points that were each calling `lexically_normal()` for themselves
now go through it.

Why the differential did not catch this: `std-fs` is in the suite
differential's `NEEDS_HOST_FACILITIES` set, because most of it opens files.
Excluding a test from the wasm comparison excludes the pure part of it too.
`tests/path-lexical-edges.abs` is the pure part, in its own file, so it is
compared.

### Open: there is no routine that turns a real number into text

This is one defect with three faces, and the reason it is recorded rather than
patched is that every available patch trades one wrong answer for another.

**On wasm, printing a real number prints `%g`.**

```absolute
println(format("double {}", 3.5));    // native: double 3.5
                                      // wasm:   double %g
```

Code generation lowers a `float` or a `double` to `printf("%g", ...)`. The
shim's freestanding formatter (`format_into` in `absolute_wasm_runtime.c`)
implements `%s`, `%c`, `%d`, `%i`, `%u` and `%x`, and prints any other
directive literally, on purpose, so that an unsupported one stays visible.
`%g` is unsupported. Every real number printed by a wasm program is the text
`%g`.

Section 5's WASM sweep built every test in `tests/` for both targets and diffed
the output; this survived it because no test in the corpus prints a real
number. That is the same limit section 20 recorded, and it is now recorded
twice, which is the point at which it stops being a coincidence: **building the
same corpus twice proves the corpus.**

**The two targets write JSON numbers differently.** Native uses
`std::ostringstream` at its default precision; wasm hand-rolls six decimal
places. For `123456789.25`, native writes `1.23457e+08` and wasm writes
`123456789.25`. Both are legal JSON; they are not the same text.

**Six significant digits is data loss in a serializer.** `0.1234567890123`
round-trips through `parse` and `stringify` as `0.123457`. For `println` this
is a matter of taste -- section 1 has that entry, and both answers are
defensible. For a data interchange format it is not: a document is supposed to
survive being read and written.

What the patches would cost, which is why none of them is here. Making wasm
print *something* for `%g` means writing a float formatter; making it match
glibc's `%g` digit for digit is the hard part of writing one, and a formatter
that is close but not identical replaces a loud `%g` with a last digit that
quietly differs between targets -- worse, because it is not discoverable.
Making wasm's JSON writer refuse to write a number it cannot write exactly
turns `1.0 / 3.0` into `null`, which is a different kind of data loss.

The thing that is actually missing is one shortest-round-trip double-to-text
routine, compiled into both runtimes, used by `PreparePrintable`, by
`absolute_string_builder_append_double`, and by both JSON writers. Then the
targets agree by construction rather than by matching each other's rounding,
and section 1's open entry is answered at the same time. On the host that is
`std::to_chars`; in the shim it is Dragon4 or Ryu plus a correctly rounded
decimal parser to check against, and that is a component, not a patch.

Until it exists, the invalid output and the undefined behaviour are gone --
wasm writes `null` where it cannot write the number, rather than `0.` -- and
what remains is a value that is imprecise and a target that prints `%g`. Both
are visible, and neither pretends to be right.

One thing the shim still does that is wrong, left alone for the same reason:
`5e-7` writes as `0.000001`, because rounding to six decimal places is all its
number path can do. That is a wrong number rather than invalid text, which is
the trade named above.

`tests/json-text-edges.abs` and `tests/path-lexical-edges.abs` pin the eight
that are fixed, on both targets: the escapes, the surrogate pair and each half
of it on its own, the non-finite values, `1e19` and `1e-7` past the cast, and
each path spelling above.

## 23. The method that worked

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

## 24. Environment note

During this work the container repeatedly reverted the working tree to an older
commit and deleted the build directory. Pushed commits were never affected, but
a stale build silently produced misleading results once — probe values read as
zero because the compiler predated a fix, which looked like a defect and was
not.

Check `git log --oneline -1` before trusting a probe, and rebuild after any
unexplained result.
