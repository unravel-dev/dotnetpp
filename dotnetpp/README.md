# dotnetpp - unified .NET embedding API

`dotnetpp` is a header-only layer that exposes a single `dotnet::` API on top
of one of two backends, selected at compile time:

- **mono** - maps onto `monopp` (`mono::`)
- **coreclr** - maps onto `clrpp` (`clr::`), a CoreCLR host built on
  `hostfxr` plus a managed bridge assembly (`Clrpp.Managed.dll`)

Select the backend with the CMake cache variable:

```
cmake -DDOTNETPP_BACKEND=coreclr ...   # default
cmake -DDOTNETPP_BACKEND=mono    ...
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

Both backends share the `to_managed` / `from_managed` converter protocol.

## Semantic differences between the backends

### Internal calls

- **mono**: managed methods are declared
  `[MethodImpl(MethodImplOptions.InternalCall)] extern ...` and the runtime
  binds them to `mono::add_internal_call` registrations automatically.
- **coreclr**: the runtime has no public icall table, so
  `[MethodImpl(InternalCall)]` cannot be used by user assemblies as-is.
  Instead, `dotnet::compile` (and `dotnet::weave_assembly`) rewrites those
  extern methods with a Mono.Cecil weaver that emits a real body: resolve
  the registered native function, marshal args, `calli`, release resources,
  and check the pending exception. Source stays mono-style:

  ```csharp
  [MethodImpl(MethodImplOptions.InternalCall)]
  public extern string ReturnAString(string value);   // works on both backends
  ```

  A low level escape hatch also exists: `InternalCalls.Get(name)` returns the
  raw pointer for use with `delegate* unmanaged[Cdecl]`, with manual
  marshalling helpers (`AllocUtf8`/`FreeUtf8`/`ConsumeUtf8`,
  `AllocHandle`/`FreeHandle`/`ConsumeHandle`) and
  `InternalCalls.ThrowIfPending()` for the pending exception model (CoreCLR
  cannot throw managed exceptions across the unmanaged boundary, so
  `clr::raise_exception` stores the exception until the managed caller
  re-throws it).

#### `[InternalCall]` IL weaving on coreclr (compile step)

So the C# side does not change at all, the coreclr backend weaves mono-style
icalls as part of compilation (Mono.Cecil based, no mono runtime involved).
`dotnet::compile` automatically rewrites the produced assembly: every
`[MethodImpl(MethodImplOptions.InternalCall)] extern` method gets a real
body that binds the registered native function and calls it through a
statically-typed `calli` - so mono-style icall declarations (including
extern constructors) compile and run unchanged.

- Name lookup follows mono's order: the full signature name
  (`"Tests.MyObject::ReturnAString(string)"`) first, then the bare name
  (`"Tests.MyObject::ReturnAString"`), matching how registrations are
  written for `mono::add_internal_call`.
- The woven body marshals inline (strings as utf8 allocations, objects as
  GCHandles, bools and chars widened to int32, by-ref value types as pinned
  raw pointers, structs by value) and contains no runtime code generation,
  so woven assemblies work on AOT-only platforms.
- Structs cross by value so that a managed wrapper struct stays
  interchangeable with a native scalar of the same size (the engine's
  `Entity{uint}` maps onto `entt::entity` this way). Because the runtime
  interop-marshals by-value structs in a `calli` signature (bool as 4-byte
  BOOL, char as 1-byte ANSI by default), the weaver annotates bool/char
  fields of every struct reachable from an icall signature with
  `MarshalAs(U1/U2)`, making the interop layout byte-identical to the raw
  CLR/C++ one. Structs defined in a different, unwoven assembly that lack
  those annotations are rejected with a warning.
- Icalls already registered natively are pre-bound when the assembly loads
  (a synthetic `Clrpp.Woven.IcallBinder.BindAll()` runs after load); icalls
  registered later bind lazily on first invocation, so registration order
  requirements are identical to mono.
- `dotnet::weave_assembly(path)` runs the same rewrite on an existing dll
  (rewrites dll/pdb in place; a no-op returning true on the mono backend).
  Requires `Mono.Cecil.dll` next to `Clrpp.Managed.dll`.
- Unsupported shapes (generic methods/types, pointer parameters, by-ref
  reference types, instance icalls on structs, and structs containing
  object references - those cannot cross by raw copy) are skipped with a
  warning and fail at invocation time, exactly as an unwoven extern would.

### CoreCLR / Mono interpreter (`interpreter_config`)

`dotnet::init` takes an optional `interpreter_config` applied before the
runtime loads. Pre-existing `DOTNET_*` / process env values are never
overridden.

| mode | effect |
| ---- | ------ |
| `automatic` (default) | leave the environment alone |
| `forced` | CoreCLR: `DOTNET_InterpMode=1`; Mono: pass `--interpreter` to `mono_jit_init` |

Use `forced` only to exercise the interpreter on a JIT-capable desktop
runtime. On no-JIT packs (e.g. iOS) the runtime enables the interpreter
itself — leave `automatic`. Shipped release CoreCLR builds may not include
`FEATURE_INTERPRETER`; the switch is then inert unless you point at a
checked runtime.

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
  `Microsoft.NETCore.App.Ref` pack, then weaves mono-style icalls in the
  output (see above). Fixtures only need to reference `Clrpp.Managed.dll`
  when they use `InternalCalls.Get` directly; woven mono-style icalls
  need no compile time reference (the weaver adds the assembly reference).

### Test suite composition

`tests/` builds a single unified suite (`tests/dotnetpp_suite.cpp`) against
one managed fixture (`tests/managed/tests.cs`); both compile and run
unchanged on either backend. The suite covers assembly loading and
compilation, type/method/field/property reflection and invocation, enums,
inheritance, primitive/string/POD marshalling, arrays, lists, boxing,
internal calls (including native exceptions caught in managed code), gc
handles and gc stats.
