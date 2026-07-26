# Changelog

## 0.4.0

- Added the O(1) ring-buffer `Deque<T>` collection with indexed access,
  snapshots, iteration, and insertion/removal at both ends.
- Added typed FIFO `Queue<T>` and LIFO `Stack<T>` facades.
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
