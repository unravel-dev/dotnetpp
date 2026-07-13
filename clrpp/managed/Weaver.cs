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
    [UnmanagedCallersOnly]
    public static void SetInternalCallWeaving(int enabled)
    {
        IcallWeaver.Enabled = enabled != 0;
    }
}

/// <summary>
/// Optional IL weaving that makes mono-style internal calls work unchanged on
/// CoreCLR.
///
/// Assemblies loaded through ClrppLoadContext are scanned for
/// [MethodImpl(MethodImplOptions.InternalCall)] extern methods. Each one gets
/// a real body that lazily resolves the native function through
/// InternalCalls.BindWoven - trying the mono-style name with the argument
/// signature ("Ns.Type::Method(single,string)") first, then the bare name -
/// and forwards to the bound delegate. C# written against the mono backend
/// ([MethodImpl(InternalCall)] extern ...) therefore runs without changes.
///
/// The rewrite happens in memory at assembly load time; files on disk are
/// never modified and the compile pipeline stays untouched. The feature is
/// optional twice over: it only rewrites methods explicitly marked as
/// internal calls, and it disables itself when Mono.Cecil.dll is not deployed
/// next to the bridge. Native code can also switch it off through
/// clr::set_internal_call_weaving(false).
///
/// Unsupported shapes (generic methods/types, by-ref or pointer parameters,
/// instance methods on value types, more than 16 parameters) are left alone
/// with a warning and keep failing at invocation time, exactly as an unwoven
/// extern would.
/// </summary>
internal static class IcallWeaver
{
    internal static bool Enabled = true;

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

    private static bool WeaveMethod(MethodDefinition method, WellKnownReferences refs, int index)
    {
        var type = method.DeclaringType;
        var module = refs.Module;

        if (method.HasGenericParameters || type.HasGenericParameters ||
            (type.IsValueType && !method.IsStatic))
        {
            Warn(method, "unsupported declaring shape");
            return false;
        }

        // Delegate argument list: `this` travels as object (marshalled as a
        // GCHandle by InternalCalls.Bind, matching the native icall ABI).
        var argTypes = new List<TypeReference>();
        if (!method.IsStatic)
        {
            argTypes.Add(module.TypeSystem.Object);
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
            // they travel as a raw pointer to the struct, matching mono's
            // icall ABI. They force a custom delegate type since Func/Action
            // cannot carry by-ref generic arguments.
            if (parameterType.IsByReference)
            {
                var element = ((ByReferenceType)parameterType).ElementType;
                if (!element.IsValueType)
                {
                    Warn(method, $"unsupported parameter type {parameterType.FullName}");
                    return false;
                }
            }
            argTypes.Add(parameterType);
        }

        var returnType = method.ReturnType;
        bool hasReturn = returnType.MetadataType != MetadataType.Void;
        if (returnType.IsByReference || returnType.IsPointer)
        {
            Warn(method, $"unsupported return type {returnType.FullName}");
            return false;
        }

        if (argTypes.Count > 16)
        {
            Warn(method, "too many parameters");
            return false;
        }

        var delegateType = MakeDelegateType(refs, argTypes, hasReturn ? returnType : null, out var invoke);

        var field = new FieldDefinition(
            "<icall>" + method.Name.Replace('.', '_') + "_" + index,
            FieldAttributes.Private | FieldAttributes.Static,
            delegateType);
        type.Fields.Add(field);

        // Registered name candidates, mono lookup order: full signature
        // ("Ns.Type::Method(single,string)"), then the bare name.
        var bare = type.FullName + "::" + method.Name;
        var primary = bare + "(" + string.Join(",", method.Parameters.Select(p => MonoTypeName(p.ParameterType))) + ")";

        method.ImplAttributes &= ~MethodImplAttributes.InternalCall;
        method.Body = new MethodBody(method);
        var il = method.Body.GetILProcessor();

        // An extern instance ctor never chained to the base ctor under mono
        // (the icall was the whole body); on CoreCLR the woven body does the
        // proper thing and calls the parameterless base ctor first.
        if (method.IsConstructor && !method.IsStatic)
        {
            var baseCtor = new MethodReference(".ctor", module.TypeSystem.Void, type.BaseType)
            {
                HasThis = true,
            };
            il.Emit(OpCodes.Ldarg_0);
            il.Emit(OpCodes.Call, baseCtor);
        }

        // Lazily bind on first call (mono resolves icalls on first invocation
        // too, so registration order requirements stay identical). The race
        // between threads is benign - both bind the same target.
        var ready = il.Create(OpCodes.Ldsfld, field);

        il.Emit(OpCodes.Ldsfld, field);
        il.Emit(OpCodes.Brtrue, ready);
        il.Emit(OpCodes.Ldstr, primary);
        il.Emit(OpCodes.Ldstr, bare);
        il.Emit(OpCodes.Ldtoken, delegateType);
        il.Emit(OpCodes.Call, refs.GetTypeFromHandle);
        il.Emit(OpCodes.Call, refs.BindWoven);
        il.Emit(OpCodes.Castclass, delegateType);
        il.Emit(OpCodes.Stsfld, field);

        il.Append(ready);
        if (!method.IsStatic)
        {
            il.Emit(OpCodes.Ldarg_0);
        }
        foreach (var parameter in method.Parameters)
        {
            il.Emit(OpCodes.Ldarg, parameter);
        }
        il.Emit(OpCodes.Callvirt, invoke);
        il.Emit(OpCodes.Ret);

        return true;
    }

    private static TypeReference MakeDelegateType(WellKnownReferences refs, List<TypeReference> argTypes,
                                                  TypeReference returnType, out MethodReference invoke)
    {
        // Func/Action cannot carry by-ref generic arguments; those signatures
        // get a purpose-built delegate type woven into the module instead.
        if (argTypes.Any(arg => arg.IsByReference))
        {
            return MakeCustomDelegateType(refs, argTypes, returnType, out invoke);
        }

        var module = refs.Module;
        var corlib = module.TypeSystem.CoreLibrary;
        bool isFunc = returnType != null;
        int genericCount = argTypes.Count + (isFunc ? 1 : 0);

        if (genericCount == 0)
        {
            var action = new TypeReference("System", "Action", module, corlib);
            invoke = new MethodReference("Invoke", module.TypeSystem.Void, action)
            {
                HasThis = true,
            };
            return action;
        }

        var open = new TypeReference("System", (isFunc ? "Func`" : "Action`") + genericCount, module, corlib);
        for (int i = 0; i < genericCount; i++)
        {
            open.GenericParameters.Add(new GenericParameter("T" + i, open));
        }

        var instance = new GenericInstanceType(open);
        foreach (var arg in argTypes)
        {
            instance.GenericArguments.Add(arg);
        }
        if (isFunc)
        {
            instance.GenericArguments.Add(returnType);
        }

        invoke = new MethodReference("Invoke",
                                     isFunc ? open.GenericParameters[genericCount - 1]
                                            : (TypeReference)module.TypeSystem.Void,
                                     instance)
        {
            HasThis = true,
        };
        for (int i = 0; i < argTypes.Count; i++)
        {
            invoke.Parameters.Add(new ParameterDefinition(open.GenericParameters[i]));
        }

        return instance;
    }

    /// <summary>
    /// Weaves a dedicated delegate type into the module for signatures that
    /// Func/Action cannot express (by-ref parameters). The runtime provides
    /// the ctor/Invoke implementations, exactly like compiler-generated
    /// delegates.
    /// </summary>
    private static TypeReference MakeCustomDelegateType(WellKnownReferences refs, List<TypeReference> argTypes,
                                                        TypeReference returnType, out MethodReference invoke)
    {
        var module = refs.Module;
        var corlib = module.TypeSystem.CoreLibrary;
        var multicast = new TypeReference("System", "MulticastDelegate", module, corlib);

        var delegateType = new TypeDefinition(
            "Clrpp.Woven",
            "<IcallDelegate>" + refs.WovenDelegateCount++,
            TypeAttributes.Sealed | TypeAttributes.NotPublic,
            multicast);

        var ctor = new MethodDefinition(
            ".ctor",
            MethodAttributes.Public | MethodAttributes.HideBySig | MethodAttributes.SpecialName |
                MethodAttributes.RTSpecialName,
            module.TypeSystem.Void)
        {
            HasThis = true,
            ImplAttributes = MethodImplAttributes.Runtime | MethodImplAttributes.Managed,
        };
        ctor.Parameters.Add(new ParameterDefinition(module.TypeSystem.Object));
        ctor.Parameters.Add(new ParameterDefinition(module.TypeSystem.IntPtr));
        delegateType.Methods.Add(ctor);

        var invokeDef = new MethodDefinition(
            "Invoke",
            MethodAttributes.Public | MethodAttributes.HideBySig | MethodAttributes.NewSlot |
                MethodAttributes.Virtual,
            returnType ?? module.TypeSystem.Void)
        {
            HasThis = true,
            ImplAttributes = MethodImplAttributes.Runtime | MethodImplAttributes.Managed,
        };
        foreach (var arg in argTypes)
        {
            invokeDef.Parameters.Add(new ParameterDefinition(arg));
        }
        delegateType.Methods.Add(invokeDef);

        module.Types.Add(delegateType);
        invoke = invokeDef;
        return delegateType;
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
        public readonly MethodReference GetTypeFromHandle;
        public readonly MethodReference BindWoven;
        public int WovenDelegateCount;

        public WellKnownReferences(ModuleDefinition module)
        {
            Module = module;
            var corlib = module.TypeSystem.CoreLibrary;

            var systemType = new TypeReference("System", "Type", module, corlib);
            var systemDelegate = new TypeReference("System", "Delegate", module, corlib);
            var runtimeTypeHandle = new TypeReference("System", "RuntimeTypeHandle", module, corlib)
            {
                IsValueType = true,
            };

            GetTypeFromHandle = new MethodReference("GetTypeFromHandle", systemType, systemType)
            {
                HasThis = false,
            };
            GetTypeFromHandle.Parameters.Add(new ParameterDefinition(runtimeTypeHandle));

            var bridgeReference = FindOrAddBridgeReference(module);
            var internalCalls = new TypeReference("Clrpp", "InternalCalls", module, bridgeReference);

            BindWoven = new MethodReference("BindWoven", systemDelegate, internalCalls)
            {
                HasThis = false,
            };
            BindWoven.Parameters.Add(new ParameterDefinition(module.TypeSystem.String));
            BindWoven.Parameters.Add(new ParameterDefinition(module.TypeSystem.String));
            BindWoven.Parameters.Add(new ParameterDefinition(systemType));
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
