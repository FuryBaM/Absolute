# Task scheduling attributes

Absolute supports portable scheduling metadata on async functions and individual
`spawn` sites. Function defaults use `@task`; a declaration-level `@spawn`
overrides only the named fields for one launch:

```absolute
@task(core = 2, priority = 1, role = "worker")
async int32 calculate(int32 value) {
    return value * 2;
}

async int32 main() {
    @spawn(core = 4, priority = 3, role = "render")
    task<int32> result = spawn calculate(21);
    return await result;
}
```

- `core` is a zero-based logical processor index. `-1` disables affinity.
- `priority` is a portable task-queue priority from `-3` through `3`. Higher
  values are dequeued first; equal-priority work is fair across role lanes and
  FIFO inside each lane.
- `role` creates/selects a named scheduling lane and is available as a logical
  label while the task runs. Equal-priority lanes are serviced round-robin,
  while work inside one lane remains FIFO.

All arguments are named compile-time literals. `@task` is valid only on async
functions or methods. `@spawn` is valid only on a variable initialized directly
with `spawn`. An omitted field inherits the `@task` value; without either
attribute the defaults are `core = -1`, `priority = 0`, and an empty role.

The runtime applies and then restores native worker-thread affinity where the
host supports it. Unsupported or unavailable affinity requests do not prevent
the task from running; the requested logical core remains visible as metadata.
The `std/task.abs` module exposes `std.task.core()`, `priority()`, `role()`, and
`hasRole(...)` for the currently executing task.
