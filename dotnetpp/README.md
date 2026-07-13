# dotnetpp - unified .NET embedding API

`dotnetpp` is a header-only layer that exposes a single `dotnet::` API on top
of one of two backends, selected at compile time:

- **mono** - maps onto `monopp` (`mono::`)
- **coreclr** - maps onto `clrpp` (`clr::`), a CoreCLR host built on
  `hostfxr` plus a managed bridge assembly (`Clrpp.Managed.dll`)

Select the backend with the CMake cache variable:

```
cmake -DDOTNETPP_BACKEND=mono    ...   # default
cmake -DDOTNETPP_BACKEND=coreclr ...
```

The native C++ surface (domains, assemblies, types, method/field/property
invokers, arrays, lists, gc handles, internal calls, compiler driver, the
POD converter layer and the converter protocol) is identical across
backends; the test suite in `tests/dotnetpp_suite.cpp` runs green on both.
The **managed (C#) side of the interop is backend specific** - see below.

## Converter registration

Alias templates cannot be specialized, so custom conversions specialize the
backend template through a macro that works on both backends:

```cpp
dotnet_register_converter_for_pod(my_vec2, managed_vec2);
```

Both backends share the `to_mono`/`from_mono` protocol names (read "mono" as
"managed").

## Semantic differences between the backends

### Internal calls

- **mono**: managed methods are declared
  `[MethodImpl(MethodImplOptions.InternalCall)] extern ...` and the runtime
  binds them to `mono::add_internal_call` registrations automatically.
- **coreclr**: the runtime has no public icall table, so
  `[MethodImpl(InternalCall)]` cannot be used by user assemblies. Instead,
  `Clrpp.InternalCalls.Bind<TDelegate>(name)` resolves the registered native
  function and emits an IL thunk with all marshalling generated
  automatically, returned as an ordinary delegate:

  ```csharp
  static readonly Func<object, string, string> returnAString =
      InternalCalls.Bind<Func<object, string, string>>("Tests.MyObject::ReturnAString");

  public string ReturnAString(string value) => returnAString(this, value);
  ```

  The generated thunk handles strings (utf8 alloc/free/consume), reference
  types (GCHandle alloc/free/consume), blittable value types by value, and
  the pending exception check. Bind resolves eagerly (throws
  `MissingMethodException` if unregistered), so keep bindings in static
  fields - C# type initializers run lazily, after the native side registered
  its calls.

  A low level escape hatch also exists: `InternalCalls.Get(name)` returns the
  raw pointer for use with `delegate* unmanaged[Cdecl]`, with manual
  marshalling helpers (`AllocUtf8`/`FreeUtf8`/`ConsumeUtf8`,
  `AllocHandle`/`FreeHandle`/`ConsumeHandle`) and
  `InternalCalls.ThrowIfPending()` for the pending exception model (CoreCLR
  cannot throw managed exceptions across the unmanaged boundary, so
  `clr::raise_exception` stores the exception until the managed caller
  re-throws it).

  The test fixture (`tests/managed/tests.cs`) does not use `Bind`; it relies
  on the IL weaver below so a single mono-style file runs on both backends.

#### Optional: `[InternalCall]` IL weaving on coreclr

For transition scenarios where the C# side should not change at all, the
coreclr backend ships an optional IL weaver (Mono.Cecil based, no mono
runtime involved). Every assembly loaded into a domain is scanned for
`[MethodImpl(MethodImplOptions.InternalCall)] extern` methods; each one gets
a generated body that lazily binds the registered native function through
`InternalCalls.BindWoven` and forwards to it - so mono-style icall
declarations (including extern constructors) run unchanged:

```csharp
[MethodImpl(MethodImplOptions.InternalCall)]
public extern string ReturnAString(string value);   // works on both backends
```

Details:

- Name lookup follows mono's order: the full signature name
  (`"Tests.MyObject::ReturnAString(string)"`) first, then the bare name
  (`"Tests.MyObject::ReturnAString"`), matching how registrations are
  written for `mono::add_internal_call`.
- The rewrite happens in memory at assembly load; files on disk are never
  modified and binding is lazy (first invocation), so registration order
  requirements are identical to mono.
- Enabled by default; toggle with `dotnet::set_internal_call_weaving(bool)`
  (a no-op on the mono backend). Requires `Mono.Cecil.dll` next to
  `Clrpp.Managed.dll` - when missing, the feature turns itself off with a
  warning and only explicit `Bind` calls keep working.
- Unsupported shapes (generic methods/types, by-ref/pointer parameters,
  instance icalls on structs, >16 parameters) are skipped with a warning.

### Domains

- **mono**: real AppDomains.
- **coreclr**: collectible `AssemblyLoadContext`s. Unloading is cooperative:
  live strong GCHandles (including wrapper gc handles) keep the context
  alive until released. Assemblies are loaded from memory streams, so the
  files on disk stay unlocked and can be recompiled while the runtime runs.
  The bridge assembly is never duplicated into a child context.

### Compiler driver (`dotnet::compile`)

- **mono**: invokes `mcs`.
- **coreclr**: invokes Roslyn (`dotnet exec csc.dll`) with a response file
  and implicit framework references from the newest installed
  `Microsoft.NETCore.App.Ref` pack. Fixtures only need to reference
  `Clrpp.Managed.dll` when they use `InternalCalls.Bind`/`Get` directly;
  woven mono-style icalls need no compile time reference (the weaver adds
  the assembly reference at load time).

### Test suite composition

`tests/` builds a single unified suite (`tests/dotnetpp_suite.cpp`) against
one managed fixture (`tests/managed/tests.cs`); both compile and run
unchanged on either backend. The suite covers assembly loading and
compilation, type/method/field/property reflection and invocation, enums,
inheritance, primitive/string/POD marshalling, arrays, lists, boxing,
internal calls (including native exceptions caught in managed code), gc
handles and gc stats.
