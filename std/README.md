# Absolute standard library (`absolute.std`)

Source tree for the Absolute `std.*` namespaces.

| | |
|--|--|
| Package | `absolute.std` **0.9.0** (`abspackage.json`) |
| Policy | [`docs/standard-library.md`](../docs/standard-library.md) |

## Quick import

```absolute
import std.core; // common application modules
import std.fs;
import std.env;
import std.collections.vector;
import std.collections.deque;
import std.collections.priority_queue;
import std.collections.hash_map;
import std.hash;
import "std/string.abs"; // defines namespace std.text
```

Launch arguments are available through `std.env.argsCount()`,
`std.env.argAt()`, `std.env.args()`, `std.env.flag()`, and
`std.env.parameter()`.

Resource-owning standard types are cleaned up automatically through
`destroy()`. `close()` and `dispose()` remain available when deterministic,
early cleanup is useful.

Filesystem paths can be composed without string concatenation:

```absolute
std.fs.Path* asset = std.fs.path("assets");
asset.push("textures");
asset.push("wall.png");

string[] files = std.fs.walk("assets");
std.fs.Metadata info = std.fs.metadata(files[0]);
```

HTTP requests use typed, case-insensitive headers and explicit request options:

```absolute
std.http.Headers* headers = new std.http.Headers();
headers.put("Accept", "application/json");
std.http.RequestOptions* options = new std.http.RequestOptions();
options.timeoutMilliseconds = 2000;
std.http.HttpResponse* response =
    std.http.request("GET", "http://localhost:8080/data", "", headers, options);
println(response.headers.value("Content-Type"));
```

Redirects, cancellation, body callbacks, and multipart form-data work over
HTTP and verified HTTPS. Windows uses WinHTTP and the Windows trust store;
Unix uses the installed system libcurl with peer and hostname verification.
WebAssembly HTTPS GET is delegated to its host.

Priority queues accept a comparator. A negative result means that `left` is
dequeued before `right`; equal priorities preserve insertion order:

```absolute
func<int32, int32, int32> ascending =
    fn(int32 left, int32 right) => left - right;
std.collections.PriorityQueue<int32>* jobs =
    new std.collections.PriorityQueue<int32>(ascending);
jobs.enqueue(20);
jobs.enqueue(10);
println(jobs.dequeue()); // 10
```

Hash collections accept explicit hashing and equality functions. Equal keys
must always produce the same hash:

```absolute
func<int64, string> hash =
    fn(string value) => std.hash.stringCode(value);
func<bool, string, string> equal =
    fn(string left, string right) => std.hash.stringEqual(left, right);
std.collections.HashMap<string, int32>* scores =
    new std.collections.HashMap<string, int32>(hash, equal);
scores["Absolute"] = 42;
```

Typed channels use an explicit 64-bit codec. Standard factories cover common
primitive messages:

```absolute
std.collections.Channel<int32>* events =
    std.collections.channels.int32Values(16);
events.send(42);
println(events.receive());
```

Managed objects cross a task boundary through a one-shot transfer capsule:

```absolute
std.concurrent.TransferChannel<Job>* jobs =
    new std.concurrent.TransferChannel<Job>();
Job* owner = new Job();
std.concurrent.Transfer<Job>* message =
    new std.concurrent.Transfer<Job>(seal(move(owner)));
jobs.send(message);
Job* received = jobs.receive();
```

Shared scheduling uses resource-owning synchronization handles:

```absolute
std.concurrent.Semaphore* slots =
    new std.concurrent.Semaphore(2, 2);
slots.acquire();
defer slots.release();

std.concurrent.RwLock* stateLock = new std.concurrent.RwLock();
stateLock.lockRead();
defer stateLock.unlockRead();
```

`ConditionVariable.wait(mutex)` atomically releases and reacquires the supplied
locked mutex. A primitive owner must outlive every task or thread waiting on its
native handle.

## Modules

See the namespace ↔ file map in `docs/standard-library.md`.

## Versioning

- SemVer on the package: breaking Stable APIs bump **MINOR** while `0.x`, **MAJOR** from `1.0.0`.
- Prefer `^0.9.0` only if you accept 0.x preview churn; pin tighter for production.
