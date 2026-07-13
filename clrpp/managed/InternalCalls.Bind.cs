using System;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.InteropServices;

namespace Clrpp
{

/// <summary>
/// Automatic binding of native internal calls.
///
/// CoreCLR has no public equivalent of mono's icall table, so
/// [MethodImpl(MethodImplOptions.InternalCall)] cannot be used by user
/// assemblies. Bind gets as close as possible at runtime: it resolves the
/// registered native function and emits a small IL thunk that performs all
/// argument/return marshalling, the raw calli, cleanup and the pending
/// exception check - the exact code one would otherwise write by hand.
///
/// Marshalling rules (matching the native clr_internal_call ABI):
///   - string          : utf8 pointer, allocated for the call and freed after;
///                       returned strings are consumed (native transfers
///                       ownership of an interop allocation)
///   - reference types : GCHandle, allocated for the call and freed after;
///                       returned objects arrive as transferred handles.
///   - value types     : passed/returned by value (must be blittable)
///
/// Usage:
///
///   static readonly Func&lt;object, string, string&gt; ReturnAString =
///       InternalCalls.Bind&lt;Func&lt;object, string, string&gt;&gt;("Tests.MyObject::ReturnAString");
///
///   public string ReturnAString(string value) => ReturnAString(this, value);
///
/// Bind resolves eagerly and throws MissingMethodException when the name is
/// not registered, so bind after the native side called clr::add_internal_call
/// (static readonly fields do this naturally - C# type initializers are lazy).
/// </summary>
public static partial class InternalCalls
{
    /// <summary>Bind a registered internal call as an ordinary delegate.</summary>
    public static TDelegate Bind<TDelegate>(string name) where TDelegate : Delegate
    {
        return (TDelegate)Bind(name, typeof(TDelegate));
    }

    /// <summary>Non-generic variant of Bind for reflection scenarios.</summary>
    public static Delegate Bind(string name, Type delegateType)
    {
        return BindPointer(Get(name), name, delegateType);
    }

    /// <summary>
    /// Binding entry point used by IL woven from mono-style
    /// [MethodImpl(MethodImplOptions.InternalCall)] extern methods (see
    /// Weaver.cs). Follows mono's lookup order: the full signature name
    /// first, then the bare "Type::Method" name.
    /// </summary>
    public static Delegate BindWoven(string primaryName, string fallbackName, Type delegateType)
    {
        var fn = TryGet(primaryName);
        var name = primaryName;
        if (fn == IntPtr.Zero)
        {
            fn = TryGet(fallbackName);
            name = fallbackName;
        }

        if (fn == IntPtr.Zero)
        {
            throw new MissingMethodException($"Internal call not registered: {primaryName}");
        }

        return BindPointer(fn, name, delegateType);
    }

    private static Delegate BindPointer(IntPtr fn, string name, Type delegateType)
    {
        var invoke = delegateType.GetMethod("Invoke")
            ?? throw new ArgumentException($"{delegateType} is not a delegate type", nameof(delegateType));

        var parameters = invoke.GetParameters();
        var returnType = invoke.ReturnType;

        var paramTypes = new Type[parameters.Length];
        for (int i = 0; i < parameters.Length; i++)
        {
            var t = parameters[i].ParameterType;
            if (t.IsPointer)
            {
                throw new ArgumentException(
                    $"Internal call '{name}': pointer parameters are not supported ({t})");
            }
            if (t.IsByRef && !t.GetElementType().IsValueType)
            {
                throw new ArgumentException(
                    $"Internal call '{name}': by-ref parameters are only supported for value types ({t})");
            }
            paramTypes[i] = t;
        }

        if (returnType.IsByRef || returnType.IsPointer)
        {
            throw new ArgumentException($"Internal call '{name}': unsupported return type {returnType}");
        }

        var thunk = new DynamicMethod(
            "clrpp_icall_" + name,
            returnType,
            paramTypes,
            typeof(InternalCalls).Module,
            skipVisibility: true);

        EmitThunk(thunk.GetILGenerator(), fn, paramTypes, returnType);

        return thunk.CreateDelegate(delegateType);
    }

    private static void EmitThunk(ILGenerator il, IntPtr fn, Type[] paramTypes, Type returnType)
    {
        var allocUtf8 = Method(nameof(AllocUtf8));
        var freeUtf8 = Method(nameof(FreeUtf8));
        var allocHandle = Method(nameof(AllocHandle));
        var freeHandle = Method(nameof(FreeHandle));
        var consumeUtf8 = Method(nameof(ConsumeUtf8));
        var consumeHandle = Method(nameof(ConsumeHandle));
        var throwIfPending = Method(nameof(ThrowIfPending));

        // Marshal arguments needing conversion into locals first, so they can
        // be released after the call.
        var nativeParamTypes = new Type[paramTypes.Length];
        var marshalled = new LocalBuilder[paramTypes.Length];
        var release = new MethodInfo[paramTypes.Length];

        for (int i = 0; i < paramTypes.Length; i++)
        {
            var t = paramTypes[i];
            if (t == typeof(string))
            {
                nativeParamTypes[i] = typeof(IntPtr);
                marshalled[i] = il.DeclareLocal(typeof(IntPtr));
                release[i] = freeUtf8;
                il.Emit(OpCodes.Ldarg, (short)i);
                il.Emit(OpCodes.Call, allocUtf8);
                il.Emit(OpCodes.Stloc, marshalled[i]);
            }
            else if (t.IsByRef)
            {
                // By-ref value type: the managed reference is already a
                // pointer to the struct - pass it through as native int,
                // matching mono's icall ABI for out/ref parameters.
                nativeParamTypes[i] = typeof(IntPtr);
            }
            else if (t == typeof(bool))
            {
                // Bools travel as int32 (see icall_abi<bool> on the native
                // side): a C++ bool only defines the low byte of the
                // register, which is not enough for the managed ABI.
                nativeParamTypes[i] = typeof(int);
            }
            else if (!t.IsValueType)
            {
                nativeParamTypes[i] = typeof(IntPtr);
                marshalled[i] = il.DeclareLocal(typeof(IntPtr));
                release[i] = freeHandle;
                il.Emit(OpCodes.Ldarg, (short)i);
                il.Emit(OpCodes.Call, allocHandle);
                il.Emit(OpCodes.Stloc, marshalled[i]);
            }
            else
            {
                nativeParamTypes[i] = t;
            }
        }

        // Push the arguments and the target pointer, then raw-call.
        for (int i = 0; i < paramTypes.Length; i++)
        {
            if (marshalled[i] != null)
            {
                il.Emit(OpCodes.Ldloc, marshalled[i]);
            }
            else if (paramTypes[i].IsByRef)
            {
                il.Emit(OpCodes.Ldarg, (short)i);
                il.Emit(OpCodes.Conv_I);
            }
            else
            {
                il.Emit(OpCodes.Ldarg, (short)i);
            }
        }

        il.Emit(OpCodes.Ldc_I8, fn.ToInt64());
        il.Emit(OpCodes.Conv_I);

        var nativeReturnType =
            returnType == typeof(void) ? typeof(void)
            : returnType == typeof(string) || !returnType.IsValueType ? typeof(IntPtr)
            : returnType == typeof(bool) ? typeof(int)
            : returnType;

        il.EmitCalli(OpCodes.Calli, CallingConvention.Cdecl, nativeReturnType, nativeParamTypes);

        LocalBuilder result = null;
        if (nativeReturnType != typeof(void))
        {
            result = il.DeclareLocal(nativeReturnType);
            il.Emit(OpCodes.Stloc, result);
        }

        for (int i = 0; i < paramTypes.Length; i++)
        {
            if (marshalled[i] != null)
            {
                il.Emit(OpCodes.Ldloc, marshalled[i]);
                il.Emit(OpCodes.Call, release[i]);
            }
        }

        il.Emit(OpCodes.Call, throwIfPending);

        if (returnType == typeof(string))
        {
            il.Emit(OpCodes.Ldloc, result);
            il.Emit(OpCodes.Call, consumeUtf8);
        }
        else if (returnType == typeof(bool))
        {
            // Normalize the int32 wire value back to a proper bool.
            il.Emit(OpCodes.Ldloc, result);
            il.Emit(OpCodes.Ldc_I4_0);
            il.Emit(OpCodes.Cgt_Un);
        }
        else if (returnType != typeof(void) && !returnType.IsValueType)
        {
            il.Emit(OpCodes.Ldloc, result);
            il.Emit(OpCodes.Call, consumeHandle);
            if (returnType != typeof(object))
            {
                il.Emit(OpCodes.Castclass, returnType);
            }
        }
        else if (returnType != typeof(void))
        {
            il.Emit(OpCodes.Ldloc, result);
        }

        il.Emit(OpCodes.Ret);
    }

    private static MethodInfo Method(string name)
    {
        return typeof(InternalCalls).GetMethod(name, BindingFlags.Public | BindingFlags.Static)
            ?? throw new MissingMethodException(nameof(InternalCalls), name);
    }
}

} // namespace Clrpp
