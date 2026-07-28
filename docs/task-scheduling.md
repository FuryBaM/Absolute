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

## Scheduler v2 runtime

Native tasks run on stackful fibers owned by a bounded OS worker pool. Once a
fiber starts, it remains pinned to that worker so thread-local state is never
migrated between OS threads. Waiting for another task, a bounded channel,
`TransferChannel`, the runtime mutex, or `std.task.delay` suspends the fiber and
returns its worker to the scheduler. Completion, channel operations, mutex
unlock, and timer expiry enqueue the suspended task again.

The default pool size is the available hardware concurrency capped at 32.
`ABSOLUTE_SCHEDULER_WORKERS=1..32` overrides it before the first task is spawned;
`std.task.workerCount()` reports the selected value. This override is intended
for constrained hosts and deterministic scheduler tests.

Windows uses native Fibers. Linux and macOS use `ucontext`; Android/Termux uses
the Termux `libucontext` package installed by the project bootstrap script.

Linux and Android/Termux use a one-shot `epoll` reactor for untimed TCP
accept/send/receive and UDP send/receive calls made by scheduler tasks. The
reactor can track multiple read and write waiters for one descriptor. A pending
socket suspends its fiber without consuming an I/O or scheduler worker;
readiness enqueues the fiber on its owner worker.

DNS resolution, connect, timed socket waits, filesystem metadata, whole-file
operations, and streaming file operations use a separate blocking I/O executor.
Windows and macOS socket operations also use this portable fallback until their
IOCP and `kqueue` backends are implemented. The calling fiber is still
suspended, so the host operation never occupies a scheduler worker. Calls made
outside an Absolute task remain synchronous.

The I/O executor defaults to the host concurrency clamped to `2..4` threads.
`ABSOLUTE_IO_WORKERS=2..32` overrides it before the first task uses the portable
blocking-offload backend. It does not limit Linux/Termux `epoll` socket
concurrency. IOCP on Windows, `kqueue` on macOS, and deadline cancellation in
the native reactors remain planned; they use the same fiber suspension
contract.
