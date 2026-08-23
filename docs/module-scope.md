# Module-scope declarations

Module scope — the top level of a file, outside every function and type — is
deliberately narrow. It holds declarations the compiler can emit once, into the
object file, before anything runs. Everything else belongs inside a function.

## What may be declared

- Functions, classes, structs, interfaces, enums, groups, namespaces, type
  aliases, imports and plugin declarations.
- Variables of a primitive type (`int8` … `uint64`, `float`, `double`, `bool`,
  `char`, `string`) whose initializer is a literal, or which have no
  initializer at all and start zeroed.
- Arrays of those primitives, with a literal initializer.

```absolute
int32 LIMIT = 64;
double RATIO = 1.5;
int32[] TABLE = {1, 2, 3};
string NAME = "absolute";
```

## What may not

**A non-constant initializer.** Nothing is folded or evaluated before the
program starts, so an expression is refused even when its value is obvious:

```absolute
int32 COUNTER = 1 + 2;   // a module-scope initializer must be a constant
                         // literal, not an expression evaluated at run time
```

**A value of a user-defined type.** A struct or class instance is built by
running its constructor, and module storage is written before anything runs, so
the declaration is rejected where it stands — with or without an initializer:

```absolute
struct Handle { int32 value; }
Handle GLOBAL;           // a module-scope declaration must be a primitive
                         // with a constant initializer
```

**Anything that owns a resource** — a managed pointer, a weak pointer or a
task. These are refused by the analyzer with `E_MODULE_SCOPE_OWNER`, and for a
reason beyond emission: module storage is never destroyed, so the destruction
point of an owner living there, and its order against other globals, would be
undefined. See [resource-ownership.md](resource-ownership.md).

```absolute
Point* SHARED = new Point();   // E_MODULE_SCOPE_OWNER
```

## Where to put the rest

A value that needs construction, or an initializer that needs computing,
belongs inside a function — typically `main`, or a function that returns it:

```absolute
struct Handle { int32 value; }

Handle makeHandle() {
    Handle result;
    result.value = 3;
    return result;
}

int32 main() {
    Handle local = makeHandle();
    return local.value;
}
```

## Script files

A file without an explicit `main` may use top-level executable statements: the
compiler moves them, along with their variable declarations, into a hidden
`int32 main()` and appends `return 0`. Inside that hidden function the ordinary
rules of a function body apply, so a struct value or a computed initializer is
fine there — it is no longer at module scope.

```absolute
struct Handle { int32 value; }
Handle handle;                // inside the hidden main, not at module scope
handle.value = 7;
println(format("value={}", handle.value));
```

Combining top-level executable statements with an explicit `main` is an error:
it would create a second entry point. This is why the same declaration is
accepted in a script file and refused next to an explicit `main` — in the first
case it is a local, in the second it is a global.
