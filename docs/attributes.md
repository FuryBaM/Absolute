# Attributes and annotations

Attributes attach compile-time metadata to a declaration or an opaque plugin
block. They precede access and other declaration modifiers:

```absolute
@inline
int32 addOne(int32 value) {
    return value + 1;
}

@engine.reflect(category = "gameplay", version = 2, enabled = true)
public class Actor {
}
```

An attribute name may be qualified with dots. Arguments are compile-time
identifiers, strings, characters, numbers, or booleans. Both positional and
named arguments are supported, but positional arguments must come first and a
named argument cannot be repeated.

## Compiler attributes

- `@inline` requests LLVM `alwaysinline` on a function, method, or constructor;
- `@noinline` requests LLVM `noinline` on a function, method, or constructor;
- `@deprecated` and `@deprecated("message")` record deprecation metadata. A
  use-site warning will be added with the compiler warning system.

`@inline` and `@noinline` accept no arguments and cannot appear together.
Unknown unqualified attributes are errors, which catches misspelled compiler
attributes.

## Plugin metadata

Plugin-owned names must be qualified, for example `@shader.stage(Vertex)`.
Qualified metadata is preserved in the AST. An opaque plugin receives immutable
`AbsoluteAttributeV1` views in both `AbsoluteOpaqueValidationContextV1` and
`AbsoluteOpaqueLlvmContextV1`; the views remain valid only for the duration of
the callback. Text values retain their source spelling, including quotes on
string and character literals.

The shader example validates that `@shader.stage(Vertex)` matches its opaque
`shader Vertex { ... }` block and emits the received metadata count into its
LLVM module.

Attributes currently target declarations and opaque blocks. Parameter,
expression, and statement attributes are intentionally reserved for later
language revisions.
