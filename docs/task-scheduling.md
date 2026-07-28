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

## Structured concurrency

`std.task.TaskGroup` owns every `task<void>` passed to `add`. Passing a task is
a consuming transfer: the caller cannot await, transfer, or destroy that task
afterward. `join()` waits for all children and closes the group to new work.
`cancelAndJoin()` first marks every child as cancelled and then waits. The
compiler invokes `destroy()` automatically when the managed group leaves its
lexical scope, giving the same cancel-and-join guarantee on every exit path.

Children observe cooperative cancellation with `std.task.cancelled()`:

```absolute
async void worker() {
    while (!std.task.cancelled()) {
        std.task.delay(1);
    }
}

async int32 main() {
    {
        auto taskSet = new std.task.TaskGroup();
        task<void> child = spawn worker();
        taskSet.add(child);
    } // taskSet.destroy(): cancel + join
    return 0;
}
```

Cancellation does not forcibly interrupt user code. A child must reach a
cancellation check or finish normally. Automatic propagation into timers and
I/O belongs to the next scheduler milestone.

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

Linux and Android/Termux use a one-shot `epoll` reactor for TCP
connect/accept/send/receive and UDP send/receive calls made by scheduler tasks.
The reactor can track multiple read and write waiters and their deadlines for
one descriptor. A pending socket suspends its fiber without consuming an I/O or
scheduler worker; readiness or timeout enqueues the fiber on its owner worker.

Windows associates each runtime socket once with a process-wide I/O completion
port. Scheduler tasks use overlapped `ConnectEx`, `AcceptEx`, `WSASend`,
`WSARecv`, `WSASendTo`, and `WSARecvFrom`; their operation state and receive
buffer live with the suspended fiber until the completion packet returns it to
the scheduler queue. A deadline requests cancellation through `CancelIoEx`,
but the fiber resumes only after the final completion packet, so stack-backed
operation state remains valid. Parallel operations on one socket therefore do
not share a mutable receive buffer.

macOS has a one-shot `kqueue` reactor using `EVFILT_READ` and `EVFILT_WRITE`.
An `EVFILT_USER` control event delivers new registrations to its reactor
thread. Readiness and deadline expiry remove a waiter before its fiber resumes,
and remaining waiters are rearmed after each event. The same multi-accept,
shared-socket, deadline, and UDP scheduler regression is enabled on macOS; the
backend still requires confirmation by the hosted `macos-15` job.

DNS resolution, filesystem metadata, whole-file operations, and streaming file
operations use a separate blocking I/O executor. TCP connect suspends only for
DNS there; the connection attempt itself and timed socket waits use the native
reactor. Calls made outside an Absolute task remain synchronous.

The I/O executor defaults to the host concurrency clamped to `2..4` threads.
`ABSOLUTE_IO_WORKERS=2..32` overrides it before the first task uses the portable
blocking-offload backend. It does not limit Linux/Termux `epoll` socket
concurrency, Windows IOCP completions, or macOS `kqueue` readiness/deadlines.
