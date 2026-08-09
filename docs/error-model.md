# Error model

Status: implemented for the core language, native runtime, and async tasks.

## Decision

Absolute uses unchecked, typed language exceptions for exceptional failures.
`Result<T, E>` will remain an ordinary library abstraction for failures that
callers are expected to handle as part of normal control flow. It depends on
the future generic type system and is not the compiler's propagation ABI.

This split keeps constructors, operators, plugin-backed operations and deeply
nested calls usable without changing every return type, while still allowing
explicit `Result` APIs where failure is an expected outcome.

## Exception types and syntax

All thrown objects derive from the compiler-provided `Error` class. Exceptions
are managed pointers, so a throw transfers ownership to the exception runtime:

```absolute
class ParseError : Error {
    public ParseError(string message) {
        this.message = message;
    }
}

string parseSource(string source) {
    if (source == "") {
        throw new ParseError("source is empty");
    }
    return source;
}

try {
    println(parseSource(""));
}
catch (ParseError* error) {
    println(error.message);
}
catch (Error* error) {
    println("unexpected error");
}
finally {
    println("load finished");
}
```

Rules:

- `throw expression;` requires a managed pointer to `Error` or a derived class;
- `throw;` is a rethrow and is valid only inside a `catch` block;
- catches are tested from top to bottom; a base catch before a derived catch is
  rejected as unreachable;
- `try` requires at least one `catch` or `finally` clause;
- the catch binding owns the exception for the lifetime of that catch scope;
- leaving an unmatched handler transfers the same owner to the next handler;
- raw pointers and non-`Error` values cannot be thrown;
- exceptions are unchecked: function signatures do not contain a `throws`
  list in the first version.

The initial `Error` contract contains a `string message`. The managed runtime
stores a stable dynamic type identifier alongside the allocation handle, so a
base-typed `Error*` still selects a catch for its derived runtime class. A
`cause` field and stack trace may be added without changing source-level catch
syntax.

## Control flow and cleanup

`finally` executes exactly once after its associated `try`/`catch`, including
on normal completion, exception propagation, `return`, `break`, and `continue`.
The first implementation rejects `return`, `break`, `continue`, and `throw`
inside `finally`; this prevents cleanup code from silently replacing an active
control transfer. `defer` uses the same cleanup stack and runs before an
enclosing handler or `finally` when its lexical scope is exited.

Before an exception leaves a scope, CodeGen performs the existing managed
owner, raw owner, array, and task cleanup for that scope. A caught exception is
therefore not allowed to bypass lifetime checks. The Analyzer merges normal and
caught paths using the same value-flow maps used by `if` and `match`.

An exception that reaches `main` is reported to standard error and terminates
the process with a non-zero exit code after destroying the exception owner.

## Portable runtime ABI

Absolute does not expose C++ exceptions in generated code. MSVC and the
Itanium-style ABI used by Clang/GCC have different personalities and unwind
metadata, and a native exception must never cross an `extern "C"` boundary.

Instead, the runtime owns a thread-local exception record containing:

- the managed handle of the thrown `Error` object;
- its stable Absolute type identifier;
- whether an exception is pending.

The compiler emits explicit exceptional control-flow edges:

1. `throw` transfers the managed owner into the runtime record;
2. local scope cleanups run;
3. control branches to the nearest generated handler, or returns through the
   propagation path when the handler is in a caller;
4. potentially throwing Absolute calls test the pending flag before consuming
   their result;
5. a matching catch takes the owner and clears the pending record;
6. an unmatched catch leaves the record intact and continues propagation.

The runtime exports `absolute_error_set`, `absolute_error_pending`,
`absolute_error_type`, `absolute_error_take`, `absolute_error_discard`, and
`absolute_error_report`. Managed allocations expose
`absolute_managed_set_type`/`absolute_managed_type` for dynamic catch matching.
This produces the same LLVM control flow on Windows and Linux and does not
require target-specific landing pads.

C ABI type and ownership rules for the language boundary are documented in
[`native-c-abi.md`](native-c-abi.md).

Calls declared with `extern "C"` are non-throwing from the language's point of
view. Throwing a C++ exception through them is an ABI violation. Syntax and
opaque plugins must also catch native exceptions before returning to the host.
An `export "C"` definition is the reverse boundary: it exposes an Absolute body
under a native C symbol. The body must handle Absolute errors before returning;
the C ABI does not carry the thread-local pending-error state or native unwind
metadata to its caller.

An Absolute function or accessor may declare the `nothrow` modifier when it
guarantees that no Absolute exception can leave the callable:

```absolute
public nothrow int32 size() {
    return count;
}
```

CodeGen does not emit an `absolute_error_pending` poll after a call whose
declaration is `nothrow`. A direct `throw` in such a body is rejected with
`E_NOTHROW_THROWS`. The modifier is a trusted effect contract for calls made by
the body as well: a `nothrow` implementation must call only non-throwing
operations or handle every possible exception locally. It is intended for
small, audited hot paths; omitting the modifier preserves the conservative
exception check.

## Async behavior

An async task captures an uncaught Absolute exception in its task record rather
than aborting a worker thread. `await` transfers that exception to the awaiting
thread and rethrows it at the await expression. Destroying an un-awaited failed
task destroys its captured exception after the existing pending-task diagnostic
has been handled.

## Implemented pipeline

1. The compiler prepends the built-in `Error` definition and the parser builds
   dedicated `ThrowStmt`/`TryStmt` nodes.
2. Analyzer validates catch ordering, ownership, rethrow, and forbidden control
   transfer in `finally`.
3. Runtime holds pending errors in thread-local state and task records.
4. CodeGen lowers calls, throws, handlers, dynamic type matching, propagation,
   and cleanup edges without native C++ EH.
5. `await` restores task failures on the awaiting thread.

Positive, negative, runtime, cross-function propagation, ownership, async,
Windows, and Linux scenarios are covered by `tests/exceptions.abs`,
`tests/exceptions-errors.abs`, and `tests/unhandled-exception.abs`.
