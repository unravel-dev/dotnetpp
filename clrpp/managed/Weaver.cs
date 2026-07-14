using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using Mono.Cecil;
using Mono.Cecil.Cil;

namespace Clrpp
{

public static partial class Bridge
{
    /// <summary>
    /// Weaves mono-style internal calls in the assembly at the given path,
    /// rewriting the dll (and pdb) on disk. Invoked by the native side as
    /// part of compilation (clr::compile), never at load time.
    /// Returns 1 when methods were woven, 0 when there was nothing to do,
    /// -1 on error (details are logged).
    /// </summary>
    [UnmanagedCallersOnly]
    public static int WeaveAssembly(IntPtr pathUtf8)
    {
        var path = ReadUtf8(pathUtf8);
        try
        {
            if (string.IsNullOrEmpty(path) || !File.Exists(path))
            {
                Log($"icall weaving: assembly not found: {path}", "error");
                return -1;
            }

            return IcallWeaver.WeaveFile(path) ? 1 : 0;
        }
        catch (Exception ex)
        {
            Log($"icall weaving failed for {path}: {ex}", "error");
            return -1;
        }
    }
}

/// <summary>
/// IL weaving that makes mono-style internal calls work unchanged on CoreCLR,
/// as a post-compile step.
///
/// Every [MethodImpl(MethodImplOptions.InternalCall)] extern method gets a
/// real body that performs the full native transition itself:
///
///   - a static IntPtr field caches the native function, resolved lazily
///     through InternalCalls.GetPtr on first call (mono resolves icalls on
///     first invocation too, so registration order requirements stay
///     identical),
///   - arguments are marshalled inline (strings as utf8 allocations, objects
///     as GCHandles, bools widened to int32, by-ref value types as pinned
///     pointers - matching the native clr_internal_call ABI),
///   - the function pointer is invoked via calli, marshalled resources are
///     released and the pending native exception is rethrown.
///
/// The woven IL contains no runtime code generation whatsoever, so woven
/// assemblies are compatible with AOT-only platforms - the calli signatures
/// are statically visible to an AOT compiler.
///
/// A synthetic Clrpp.Woven.IcallBinder.BindAll() method is also woven into
/// the module. The bridge invokes it right after the assembly loads,
/// opportunistically pre-binding every icall that is already registered:
/// failures surface at load time instead of at first call (which could be
/// inside a finalizer), while icalls registered later still bind lazily.
///
/// Unsupported shapes (generic methods/types, by-ref or pointer parameters,
/// instance methods on value types) are left alone with a warning and keep
/// failing at invocation time, exactly as an unwoven extern would.
/// </summary>
internal static class IcallWeaver
{
    /// <summary>
    /// Weaves the assembly file in place (pdb rewritten alongside when it is
    /// a portable pdb; otherwise it is removed to avoid a stale mismatch).
    /// Returns true when anything was woven.
    /// </summary>
    internal static bool WeaveFile(string assemblyPath)
    {
        var assemblyBytes = File.ReadAllBytes(assemblyPath);
        var pdbPath = Path.ChangeExtension(assemblyPath, ".pdb");
        var hadPdb = File.Exists(pdbPath);
        var pdbBytes = hadPdb ? File.ReadAllBytes(pdbPath) : null;

        if (!Weave(ref assemblyBytes, ref pdbBytes))
        {
            return false;
        }

        File.WriteAllBytes(assemblyPath, assemblyBytes);
        if (pdbBytes != null)
        {
            File.WriteAllBytes(pdbPath, pdbBytes);
        }
        else if (hadPdb)
        {
            // The original pdb could not be read as portable - it no longer
            // matches the rewritten assembly.
            File.Delete(pdbPath);
        }

        return true;
    }

    /// <summary>
    /// Rewrites [InternalCall] extern methods in the given image. Returns
    /// true and replaces the buffers when anything was woven. The pdb buffer
    /// is rewritten alongside (or dropped when the original could not be
    /// read as a portable pdb).
    /// </summary>
    internal static bool Weave(ref byte[] assemblyBytes, ref byte[] pdbBytes)
    {
        using var module = ReadModule(assemblyBytes, pdbBytes, out bool hasSymbols);

        var targets = AllTypes(module)
            .SelectMany(type => type.Methods)
            .Where(method => method.IsInternalCall)
            .ToList();

        if (targets.Count == 0)
        {
            return false;
        }

        var refs = new WellKnownReferences(module);

        int woven = 0;
        foreach (var method in targets)
        {
            if (WeaveMethod(method, refs, woven))
            {
                woven++;
            }
        }

        if (woven == 0)
        {
            return false;
        }

        EmitBinder(refs);

        var output = new MemoryStream();
        var writerParams = new WriterParameters();
        MemoryStream pdbOutput = null;
        if (hasSymbols)
        {
            pdbOutput = new MemoryStream();
            writerParams.WriteSymbols = true;
            writerParams.SymbolWriterProvider = new PortablePdbWriterProvider();
            writerParams.SymbolStream = pdbOutput;
        }

        module.Write(output, writerParams);

        assemblyBytes = output.ToArray();
        pdbBytes = pdbOutput?.ToArray();
        return true;
    }

    // -----------------------------------------------------------------------

    private static ModuleDefinition ReadModule(byte[] assemblyBytes, byte[] pdbBytes, out bool hasSymbols)
    {
        if (pdbBytes != null)
        {
            try
            {
                var readerParams = new ReaderParameters
                {
                    InMemory = true,
                    ReadingMode = ReadingMode.Immediate,
                    ReadSymbols = true,
                    SymbolStream = new MemoryStream(pdbBytes),
                    SymbolReaderProvider = new PortablePdbReaderProvider(),
                };
                var module = ModuleDefinition.ReadModule(new MemoryStream(assemblyBytes), readerParams);
                hasSymbols = true;
                return module;
            }
            catch
            {
                // not a portable pdb (or mismatched) - weave without symbols
            }
        }

        hasSymbols = false;
        var plainParams = new ReaderParameters
        {
            InMemory = true,
            ReadingMode = ReadingMode.Immediate,
        };
        return ModuleDefinition.ReadModule(new MemoryStream(assemblyBytes), plainParams);
    }

    private static IEnumerable<TypeDefinition> AllTypes(ModuleDefinition module)
    {
        var stack = new Stack<TypeDefinition>(module.Types);
        while (stack.Count > 0)
        {
            var type = stack.Pop();
            yield return type;
            foreach (var nested in type.NestedTypes)
            {
                stack.Push(nested);
            }
        }
    }

    // How one argument travels across the native boundary
    // (see the ABI contract in clr_internal_call.h).
    private enum ArgKind
    {
        Value,      // blittable value type, passed by value
        Bool,       // widened to int32
        String,     // utf8 allocation, freed after the call
        Object,     // GCHandle, freed after the call
        ByRefValue, // pinned pointer to the value
    }

    private static ArgKind ClassifyArg(TypeReference type)
    {
        if (type.IsByReference)
        {
            return ArgKind.ByRefValue;
        }

        if (type.MetadataType == MetadataType.Boolean)
        {
            return ArgKind.Bool;
        }

        if (type.MetadataType == MetadataType.String)
        {
            return ArgKind.String;
        }

        return type.IsValueType ? ArgKind.Value : ArgKind.Object;
    }

    private static bool WeaveMethod(MethodDefinition method, WellKnownReferences refs, int index)
    {
        var type = method.DeclaringType;
        var module = refs.Module;
        var ts = module.TypeSystem;

        if (method.HasGenericParameters || type.HasGenericParameters ||
            (type.IsValueType && !method.IsStatic))
        {
            Warn(method, "unsupported declaring shape");
            return false;
        }

        foreach (var parameter in method.Parameters)
        {
            var parameterType = parameter.ParameterType;
            if (parameterType.IsPointer)
            {
                Warn(method, $"unsupported parameter type {parameterType.FullName}");
                return false;
            }
            // By-ref (out/ref) parameters are only supported for value types:
            // they travel as a raw (pinned) pointer to the struct, matching
            // mono's icall ABI.
            if (parameterType.IsByReference &&
                !((ByReferenceType)parameterType).ElementType.IsValueType)
            {
                Warn(method, $"unsupported parameter type {parameterType.FullName}");
                return false;
            }
        }

        var returnType = method.ReturnType;
        if (returnType.IsByReference || returnType.IsPointer)
        {
            Warn(method, $"unsupported return type {returnType.FullName}");
            return false;
        }

        // Cached native function pointer. Lives on the synthetic
        // Clrpp.Woven.IcallBinder type - NOT on the declaring type - so that
        // neither the load-time pre-bind (BindAll) nor the lazy bind below
        // triggers user static constructors (mono runs those on first real
        // use of the type, and woven code must not change that).
        var field = new FieldDefinition(
            "<icall>" + (type.FullName + "::" + method.Name).Replace('.', '_') + "_" + index,
            FieldAttributes.Assembly | FieldAttributes.Static,
            ts.IntPtr);
        refs.Binder.Fields.Add(field);

        // Registered name candidates, mono lookup order: full signature
        // ("Ns.Type::Method(single,string)"), then the bare name.
        var bare = type.FullName + "::" + method.Name;
        var primary = bare + "(" + string.Join(",", method.Parameters.Select(p => MonoTypeName(p.ParameterType))) + ")";

        refs.Bindings.Add((field, primary, bare));

        method.ImplAttributes &= ~MethodImplAttributes.InternalCall;
        method.Body = new MethodBody(method) { InitLocals = true };
        var il = method.Body.GetILProcessor();

        // An extern instance ctor never chained to the base ctor under mono
        // (the icall was the whole body); on CoreCLR the woven body does the
        // proper thing and calls the parameterless base ctor first.
        if (method.IsConstructor && !method.IsStatic)
        {
            var baseCtor = new MethodReference(".ctor", ts.Void, type.BaseType)
            {
                HasThis = true,
            };
            il.Emit(OpCodes.Ldarg_0);
            il.Emit(OpCodes.Call, baseCtor);
        }

        // Lazily bind on first call when the load-time pre-bind did not
        // resolve it yet (icall registered after the assembly loaded). The
        // race between threads is benign - both bind the same target.
        var ready = il.Create(OpCodes.Nop);
        il.Emit(OpCodes.Ldsfld, field);
        il.Emit(OpCodes.Brtrue, ready);
        il.Emit(OpCodes.Ldstr, primary);
        il.Emit(OpCodes.Ldstr, bare);
        il.Emit(OpCodes.Call, refs.GetPtr);
        il.Emit(OpCodes.Stsfld, field);
        il.Append(ready);

        // ABI argument plan: `this` travels first, as a GCHandle.
        var kinds = new List<ArgKind>();
        var sources = new List<ParameterDefinition>(); // null = `this`
        if (!method.IsStatic)
        {
            kinds.Add(ArgKind.Object);
            sources.Add(null);
        }
        foreach (var parameter in method.Parameters)
        {
            kinds.Add(ClassifyArg(parameter.ParameterType));
            sources.Add(parameter);
        }

        void LoadSource(int i)
        {
            if (sources[i] == null)
            {
                il.Emit(OpCodes.Ldarg_0);
            }
            else
            {
                il.Emit(OpCodes.Ldarg, sources[i]);
            }
        }

        // Marshal arguments needing conversion into locals first, so they
        // can be released after the call.
        var marshalled = new VariableDefinition[kinds.Count];
        var release = new MethodReference[kinds.Count];

        for (int i = 0; i < kinds.Count; i++)
        {
            switch (kinds[i])
            {
                case ArgKind.String:
                    marshalled[i] = new VariableDefinition(ts.IntPtr);
                    method.Body.Variables.Add(marshalled[i]);
                    release[i] = refs.FreeUtf8;
                    LoadSource(i);
                    il.Emit(OpCodes.Call, refs.AllocUtf8);
                    il.Emit(OpCodes.Stloc, marshalled[i]);
                    break;

                case ArgKind.Object:
                    marshalled[i] = new VariableDefinition(ts.IntPtr);
                    method.Body.Variables.Add(marshalled[i]);
                    release[i] = refs.FreeHandle;
                    LoadSource(i);
                    il.Emit(OpCodes.Call, refs.AllocHandle);
                    il.Emit(OpCodes.Stloc, marshalled[i]);
                    break;

                case ArgKind.ByRefValue:
                    // Pin for the duration of the call: the referenced value
                    // may live on the GC heap (field / array element) and
                    // must not move while native code writes through the
                    // pointer. The pin releases when the method returns.
                    marshalled[i] = new VariableDefinition(new PinnedType(sources[i].ParameterType));
                    method.Body.Variables.Add(marshalled[i]);
                    LoadSource(i);
                    il.Emit(OpCodes.Stloc, marshalled[i]);
                    break;
            }
        }

        // Push the arguments and the cached target pointer, then raw-call.
        var callSite = new CallSite(NativeReturnType(returnType, ts))
        {
            CallingConvention = MethodCallingConvention.C,
        };

        for (int i = 0; i < kinds.Count; i++)
        {
            switch (kinds[i])
            {
                case ArgKind.String:
                case ArgKind.Object:
                    il.Emit(OpCodes.Ldloc, marshalled[i]);
                    callSite.Parameters.Add(new ParameterDefinition(ts.IntPtr));
                    break;

                case ArgKind.ByRefValue:
                    il.Emit(OpCodes.Ldloc, marshalled[i]);
                    il.Emit(OpCodes.Conv_I);
                    callSite.Parameters.Add(new ParameterDefinition(ts.IntPtr));
                    break;

                case ArgKind.Bool:
                    // Bools travel as int32 (see icall_abi<bool> on the
                    // native side); a bool is already an int32 on the stack.
                    LoadSource(i);
                    callSite.Parameters.Add(new ParameterDefinition(ts.Int32));
                    break;

                default:
                    LoadSource(i);
                    callSite.Parameters.Add(new ParameterDefinition(sources[i].ParameterType));
                    break;
            }
        }

        il.Emit(OpCodes.Ldsfld, field);
        il.Emit(OpCodes.Calli, callSite);

        VariableDefinition result = null;
        if (callSite.ReturnType.MetadataType != MetadataType.Void)
        {
            result = new VariableDefinition(callSite.ReturnType);
            method.Body.Variables.Add(result);
            il.Emit(OpCodes.Stloc, result);
        }

        for (int i = 0; i < kinds.Count; i++)
        {
            if (release[i] != null)
            {
                il.Emit(OpCodes.Ldloc, marshalled[i]);
                il.Emit(OpCodes.Call, release[i]);
            }
        }

        il.Emit(OpCodes.Call, refs.ThrowIfPending);

        switch (ClassifyReturn(returnType))
        {
            case ArgKind.String:
                il.Emit(OpCodes.Ldloc, result);
                il.Emit(OpCodes.Call, refs.ConsumeUtf8);
                break;

            case ArgKind.Bool:
                // Normalize the int32 wire value back to a proper bool.
                il.Emit(OpCodes.Ldloc, result);
                il.Emit(OpCodes.Ldc_I4_0);
                il.Emit(OpCodes.Cgt_Un);
                break;

            case ArgKind.Object:
                il.Emit(OpCodes.Ldloc, result);
                il.Emit(OpCodes.Call, refs.ConsumeHandle);
                if (returnType.MetadataType != MetadataType.Object)
                {
                    il.Emit(OpCodes.Castclass, returnType);
                }
                break;

            case ArgKind.Value:
                if (result != null)
                {
                    il.Emit(OpCodes.Ldloc, result);
                }
                break;
        }

        il.Emit(OpCodes.Ret);

        return true;
    }

    private static ArgKind ClassifyReturn(TypeReference returnType)
    {
        // void maps onto Value with no result local.
        return returnType.MetadataType == MetadataType.Void ? ArgKind.Value : ClassifyArg(returnType);
    }

    private static TypeReference NativeReturnType(TypeReference returnType, TypeSystem ts)
    {
        switch (ClassifyReturn(returnType))
        {
            case ArgKind.String:
            case ArgKind.Object:
                return ts.IntPtr;
            case ArgKind.Bool:
                return ts.Int32;
            default:
                return returnType;
        }
    }

    /// <summary>
    /// Emits Clrpp.Woven.IcallBinder.BindAll(), which pre-binds every woven
    /// icall that is already registered. Invoked by the bridge right after
    /// the assembly loads; icalls that are not registered yet stay zero and
    /// bind lazily on first call.
    /// </summary>
    private static void EmitBinder(WellKnownReferences refs)
    {
        var module = refs.Module;
        var ts = module.TypeSystem;

        var bindAll = new MethodDefinition(
            "BindAll",
            MethodAttributes.Public | MethodAttributes.Static | MethodAttributes.HideBySig,
            ts.Void);

        var il = bindAll.Body.GetILProcessor();
        foreach (var (field, primary, bare) in refs.Bindings)
        {
            il.Emit(OpCodes.Ldstr, primary);
            il.Emit(OpCodes.Ldstr, bare);
            il.Emit(OpCodes.Call, refs.TryGetPtr);
            il.Emit(OpCodes.Stsfld, field);
        }
        il.Emit(OpCodes.Ret);

        refs.Binder.Methods.Add(bindAll);
        module.Types.Add(refs.Binder);
    }

    /// Mono's shorthand type names as used in mono_add_internal_call
    /// signatures (must match the native types::get_types() table).
    private static string MonoTypeName(TypeReference type)
    {
        if (type is ArrayType array)
        {
            return MonoTypeName(array.ElementType) + "[]";
        }

        switch (type.MetadataType)
        {
            case MetadataType.SByte: return "sbyte";
            case MetadataType.Byte: return "byte";
            case MetadataType.Int16: return "short";
            case MetadataType.UInt16: return "ushort";
            case MetadataType.Int32: return "int";
            case MetadataType.UInt32: return "uint";
            case MetadataType.Int64: return "long";
            case MetadataType.UInt64: return "ulong";
            case MetadataType.Boolean: return "bool";
            case MetadataType.Single: return "single";
            case MetadataType.Double: return "double";
            case MetadataType.Char: return "char";
            case MetadataType.String: return "string";
            case MetadataType.Object: return "object";
            case MetadataType.IntPtr: return "intptr";
            case MetadataType.UIntPtr: return "uintptr";
            default: return type.FullName;
        }
    }

    private static void Warn(MethodDefinition method, string reason)
    {
        Bridge.Log($"icall weaving: skipping {method.DeclaringType.FullName}::{method.Name} ({reason})",
                   "warning");
    }

    /// Metadata references shared by all rewrites in one module. Everything
    /// is built against the module's own core library scope so woven
    /// assemblies keep resolving through the usual type forwarders.
    private sealed class WellKnownReferences
    {
        public readonly ModuleDefinition Module;
        public readonly MethodReference GetPtr;
        public readonly MethodReference TryGetPtr;
        public readonly MethodReference AllocUtf8;
        public readonly MethodReference FreeUtf8;
        public readonly MethodReference AllocHandle;
        public readonly MethodReference FreeHandle;
        public readonly MethodReference ConsumeUtf8;
        public readonly MethodReference ConsumeHandle;
        public readonly MethodReference ThrowIfPending;

        /// Synthetic type holding the cached function pointer fields and
        /// BindAll(). Added to the module by EmitBinder once anything wove.
        public readonly TypeDefinition Binder;

        public readonly List<(FieldDefinition Field, string Primary, string Bare)> Bindings = new();

        public WellKnownReferences(ModuleDefinition module)
        {
            Module = module;
            var ts = module.TypeSystem;

            Binder = new TypeDefinition(
                "Clrpp.Woven",
                "IcallBinder",
                TypeAttributes.NotPublic | TypeAttributes.Abstract | TypeAttributes.Sealed |
                    TypeAttributes.Class,
                ts.Object);

            var bridgeReference = FindOrAddBridgeReference(module);
            var internalCalls = new TypeReference("Clrpp", "InternalCalls", module, bridgeReference);

            MethodReference Helper(string name, TypeReference returnType, params TypeReference[] argTypes)
            {
                var reference = new MethodReference(name, returnType, internalCalls)
                {
                    HasThis = false,
                };
                foreach (var argType in argTypes)
                {
                    reference.Parameters.Add(new ParameterDefinition(argType));
                }
                return reference;
            }

            GetPtr = Helper("GetPtr", ts.IntPtr, ts.String, ts.String);
            TryGetPtr = Helper("TryGetPtr", ts.IntPtr, ts.String, ts.String);
            AllocUtf8 = Helper("AllocUtf8", ts.IntPtr, ts.String);
            FreeUtf8 = Helper("FreeUtf8", ts.Void, ts.IntPtr);
            AllocHandle = Helper("AllocHandle", ts.IntPtr, ts.Object);
            FreeHandle = Helper("FreeHandle", ts.Void, ts.IntPtr);
            ConsumeUtf8 = Helper("ConsumeUtf8", ts.String, ts.IntPtr);
            ConsumeHandle = Helper("ConsumeHandle", ts.Object, ts.IntPtr);
            ThrowIfPending = Helper("ThrowIfPending", ts.Void);
        }

        private static AssemblyNameReference FindOrAddBridgeReference(ModuleDefinition module)
        {
            var bridgeName = typeof(Bridge).Assembly.GetName();
            foreach (var reference in module.AssemblyReferences)
            {
                if (reference.Name == bridgeName.Name)
                {
                    return reference;
                }
            }

            // Assemblies that never referenced the bridge (pure mono fixtures)
            // gain the reference; ClrppLoadContext.OnResolving maps it to the
            // already-loaded bridge instance.
            var added = new AssemblyNameReference(bridgeName.Name, bridgeName.Version);
            module.AssemblyReferences.Add(added);
            return added;
        }
    }
}

} // namespace Clrpp
