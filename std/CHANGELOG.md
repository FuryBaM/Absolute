# Changelog

## 0.5.0

- Added structured concurrency through `std.task.TaskGroup`. Adding a child
  transfers its task handle to the group; `join()` waits for every child and
  `cancelAndJoin()` cooperatively cancels before joining.
- Added `std.task.cancelled()` so child tasks can observe group cancellation.

## 0.4.0

- Added the O(1) ring-buffer `Deque<T>` collection with indexed access,
  snapshots, iteration, and insertion/removal at both ends.
- Added typed FIFO `Queue<T>` and LIFO `Stack<T>` facades.
- Added stable binary-heap `PriorityQueue<T>` with a user comparator and
  O(log n) enqueue/dequeue.
- Added open-addressed `HashMap<K, V>` and `HashSet<T>` with explicit portable
  hashing/equality callbacks, tombstone reuse, and snapshot iteration.
- Added built-in `std.hash` functions for integer, Boolean, and Unicode string
  keys.
- Replaced the untyped integer channel facade with codec-based `Channel<T>`,
  including checked close/receive semantics and queue counts.
- Added `seal(move(owner))`, typed `unseal<T>`, `Transfer<T>`, and
  `std.concurrent.TransferChannel<T>` for one-shot ownership transfer. Sealing
  rotates the managed generation so aliases left in the sender expire.
- Added native and WebAssembly regression coverage for the new collections.

## 0.3.0

- Added `std.core` as a convenient import bundle for common application APIs.
- Standardized automatic resource cleanup through `destroy()`, with compatible
  `close()` and `dispose()` aliases.
- Made text searching and slicing Unicode code-point based and added explicit
  UTF-8 `byteCount()`.
- Added the complete `std.text` and `StringBuilder` runtime surface to
  WebAssembly targets.
- Added common text, filesystem, JSON, collection, environment, and process
  convenience APIs.
- Added cross-platform path composition, lexical `.` / `..` navigation, path
  metadata, and the stateful `std.fs.Path` helper on native and WebAssembly.
- Corrected HTTP `Content-Length` for non-ASCII UTF-8 bodies.

## 0.2.0

- Added portable launch arguments through `std.env`.
