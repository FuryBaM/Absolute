# Async methods

Classes and structs may declare static and instance `async` methods. They use
the same one-shot `task<T>`, `spawn`, and `await` model as namespace functions:

```absolute
class Worker {
    int32 base;

    public Worker(int32 value) {
        base = value;
    }

    public async const int32 calculate(int32 extra) {
        return base + extra;
    }
}

async int32 main() {
    const Worker* worker = new Worker(40);
    task<int32> result = spawn worker.calculate(2);
    return await result;
}
```

Static async methods need no receiver. An instance async method must be `const`
and its spawn receiver must be a named `const` value or managed owner. Raw
pointers, managed subscribers, temporaries, mutable bindings, and receiver types
that own managed pointers, arrays, or other resources are rejected at compile
time. These restrictions keep the captured `this` address valid until the
mandatory `await` without introducing a new runtime pointer kind.

Virtual class methods retain normal virtual dispatch: the task thunk resolves
the vtable slot after decoding the receiver. Struct methods use their direct
method ABI. Scalar/enum argument and result restrictions remain unchanged.

`@task(core, priority, role)` works on async methods, and a spawn-site
`@spawn(...)` may override those defaults for one method launch. An override
must preserve the inherited method's `async` contract.
