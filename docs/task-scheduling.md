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
host supports it. Unsupported, unavailable, or OS-rejected affinity requests
do not prevent the task from running; `std.task.core()` continues to report the
requested logical core as scheduling metadata.

`std.task.affinitySupported()` reports whether the runtime has a native
affinity backend. `std.task.coreAvailable(core)` checks a concrete logical core
against the capability snapshot captured before workers start. Linux and
Termux use the inherited allowed CPU mask, including non-contiguous container
CPU sets. Windows maps the zero-based logical index across processor groups.
macOS and WASM currently report affinity as unsupported.

Inside a running task, `std.task.affinityApplied()` reports whether the current
worker was actually pinned for that execution slice. It can be false even when
`coreAvailable(core)` is true if the OS or host policy rejects the request;
that is the explicit best-effort fallback. The original worker affinity is
restored whenever the fiber returns to the scheduler.

The `std/task.abs` module also exposes `priority()`, `role()`, and
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
cancellation check or finish normally.

## Cancellation and deadline propagation

Every native task owns a small reference-counted control node pointing only to
its parent node. `spawn` links the new task to the currently running task.
Cancellation and deadlines are therefore inherited through the complete task
tree without retaining task handles, introducing cycles, or imposing borrowing
rules on application values.

`std.task.setDeadlineAfter(milliseconds)` sets a monotonic deadline on the
current task. A later call may tighten but never extend an existing deadline.
All descendants observe the earliest deadline in their ancestry.
`std.task.deadlineRemaining()` returns rounded-up milliseconds, `0` after the
deadline, and `-1` when the task tree has no deadline. An expired deadline also
makes `std.task.cancelled()` return `true`.

```absolute
async void child() {
    // This delay is capped by the inherited parent deadline.
    std.task.delay(10000);
    assert(std.task.cancelled(), "deadline reached");
}

async void parent() {
    std.task.setDeadlineAfter(50);
    task<void> work = spawn child();
    await work;
}
```

Scheduler delays and native TCP/UDP connect, accept, send, and receive waits use
the earlier of their operation timeout and the inherited task deadline.
Cancellation remains cooperative: it does not unwind arbitrary user code,
force-release a held mutex, or terminate a blocking DNS/filesystem system call.
Those calls retain memory safety and expose the expired/cancelled state when
control returns to the task.

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

Each worker owns priority and role lanes. External submissions are distributed
round-robin, while a task's nested spawns enter its worker's local queue. An
idle worker visits the other queues through a rotating victim cursor and steals
ready work. Only tasks whose fiber has not started may be stolen. After its
first execution a fiber is pinned to its owner queue, so a suspended
continuation cannot migrate across threads or invalidate thread-local state.

Runnable priority levels use smooth weighted scheduling. Priorities `-3..3`
have weights `1, 2, 3, 4, 6, 8, 12`: higher priorities receive a statistical
advantage, while every continuously runnable level retains a non-zero share and
cannot be starved by a permanent high-priority backlog. Roles at one priority
are served round-robin. When both pinned continuations and new local tasks exist
at the chosen priority, the scheduler alternates between those sources.
Stealers apply the same weighted policy to work that is allowed to migrate.

These rules intentionally do not promise an exact global execution order:
multiple workers, wake-ups, timers, and stealing make that order brittle.
Fairness applies whenever control returns to the scheduler. A task that never
suspends, waits, or finishes is not preempted and can still occupy its OS
worker.

## Scheduler metrics

`std.task.metrics()` returns a managed `SchedulerMetrics` snapshot. The
`runnable` and `suspended` fields are current gauges. The remaining fields are
cumulative from scheduler startup:

- `completed` and `queueSamples`;
- `queueLatencyTotalNanoseconds` and `queueLatencyMaxNanoseconds`;
- `workerBusyNanoseconds` and `workerUtilizationPermille` (`0..1000`);
- `steals`, `wakeUps`, `blockedNanoseconds`, and `starvationEvents`.

`averageQueueLatencyNanoseconds()` derives the average from the total and sample
count. One queue sample is recorded whenever a runnable task is selected.
`blockedNanoseconds` records completed suspension intervals. A starvation event
is a queue episode whose latency reaches 100 milliseconds; it is a diagnostic
counter, not a scheduler panic.

Snapshots read lock-free counters and can therefore be slightly approximate
while workers are active. A currently running execution slice contributes to
busy time after it returns control to the scheduler. Utilization divides the
accumulated busy time across all configured worker capacity since scheduler
startup.

Windows uses native Fibers. Linux and macOS use `ucontext`; Android/Termux uses
the Termux `libucontext` package installed by the project bootstrap script.

The native scheduler stress harness continuously exercises nested spawn/await,
completion racing cancellation and timed observation, destroy waiting for
completion, channel close wake-ups, and recursively nested task groups. A task
handle remains a linear ownership token: exactly one owner may consume it with
`await`, `destroy`, or transfer it into a `TaskGroup`. Concurrently consuming
the same raw handle twice is intentionally outside the runtime contract.

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
