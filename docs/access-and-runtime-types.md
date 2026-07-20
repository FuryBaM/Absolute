# Access control and runtime type operations

## Member access

Fields, methods, static members, and constructors accept one of `public`,
`protected`, or `private`. A member without an access modifier is public for
compatibility with existing Absolute source.

- `public` is accessible from every valid use site.
- `protected` is accessible inside the declaring class and its derived classes.
- `private` is accessible only inside the declaring class or struct.

Access is checked after overload resolution, so an inaccessible overload is
reported instead of silently selecting another declaration. The rules also
apply to unqualified field references inside a type, inherited members,
instance and static access, `new`, local value construction, and `base(...)`.

Interface methods are public contracts. A private or protected interface
method, or a non-public class implementation, is rejected. An override cannot
reduce public access to protected/private or protected access to private.
Struct members may be public or private; protected struct members are rejected
because structs do not participate in inheritance. The recognized `internal`
modifier remains unsupported and produces an explicit diagnostic.

An interface method may contain a default body. Class methods take precedence
over defaults during vtable construction. A default inherited repeatedly from
the same declaration is unambiguous, including diamond inheritance. If separate
interfaces provide different defaults for the same signature, the implementing
class must declare a public method to resolve the conflict. Abstract declarations
in a child interface replace an inherited default for that contract.

## Runtime type tests

`is` tests the dynamic class of a raw or managed class/interface pointer:

```absolute
raw Node* node = new raw AddNode();
assert(node is AddNode);
assert(node is Node);
assert(node is IEvaluable);
```

The target may be written as a bare class/interface (`AddNode`) or as a pointer
type (`AddNode*`). Null and expired managed references produce `false`; no object
memory is dereferenced before the null/validity check.

## Safe casts

For class and interface references, `as` performs the same runtime test and
returns the original reference with a narrower static type, or null:

```absolute
AddNode* add = node as AddNode;
raw AddNode* rawAdd = rawNode as raw AddNode*;
IEvaluable* contract = node as IEvaluable;
```

The operation cannot convert between raw and managed pointer modes. A managed
cast result is an alias/subscriber and expires with the original owner. When a
fresh raw or managed allocation is cast directly and the test fails, the
temporary allocation is released before null is returned. Ordinary numeric
conversions such as `value as int64` keep their previous compile-time behavior.

Runtime matching compares the object's unique class vtable against the set of
classes assignable to the requested class or interface. It performs no wrapper
allocation and works for base-class tests, downcasts, and interface cross-casts.
