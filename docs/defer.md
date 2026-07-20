# Deferred cleanup

`defer` registers cleanup code in the current lexical scope. Registered actions
run in reverse order when that scope is left:

```absolute
int32 readValue() {
    raw int32* value = new raw int32(42);
    defer delete value;
    return *value;
}
```

Both a single statement and a compound block are accepted:

```absolute
defer close(handle);
defer {
    flush(log);
    close(log);
}
```

## Semantics

- arguments and variable values are read when the deferred body executes;
- names must already be visible where `defer` is declared;
- actions in one scope execute in last-in, first-out order;
- cleanup runs on normal scope completion, `return`, `break`, `continue`, and
  exception propagation;
- a defer inside a `try` body runs before the matching `catch` and `finally`;
- if a call made by a deferred action throws, older deferred actions still run;
- direct `return`, `break`, `continue`, and `throw` are rejected inside a
  deferred body;
- `delete` of a raw owner and `await` of a task count as guaranteed lifetime
  cleanup, so they satisfy the Analyzer's exit checks;
- scheduling the same raw deletion or task await more than once is rejected.

The compiler lowers deferred actions into the same scope cleanup stack used by
managed owners, tasks, and exception handlers. No callback allocation or
runtime registration is required.
