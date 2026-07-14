using System;
using System.Collections.Generic;
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
///   - scalars         : passed/returned by value (bools and chars widened
///                       to int32)
///   - structs         : passed/returned by value. Must be blittable, and
///                       bool/char fields must carry [MarshalAs(U1/U2)] so
///                       the interop layout matches the raw CLR/C++ one
///                       (the weaver annotates icall structs automatically;
///                       hand-written structs used with Bind need the
///                       attributes spelled out).
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
///
/// Bind requires a JIT: the thunk is emitted at runtime via DynamicMethod.
/// It is the only JIT-dependent API in the bridge - on AOT-only platforms
/// (iOS, NativeAOT) it throws with a pointer at the weave path, which bakes
/// the identical thunk into the assembly at compile time and needs no
/// runtime code generation.
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

    private static Delegate BindPointer(IntPtr fn, string name, Type delegateType)
    {
        if (!System.Runtime.CompilerServices.RuntimeFeature.IsDynamicCodeSupported)
        {
            throw new PlatformNotSupportedException(
                $"Internal call '{name}': InternalCalls.Bind emits IL at runtime, which this " +
                "platform does not allow. Declare the method as a mono-style " +
                "[MethodImpl(MethodImplOptions.InternalCall)] extern instead - the compile-time " +
                "weaver gives it an AOT-compatible body with no runtime code generation.");
        }

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
            // Value types cross by raw copy - reference fields inside would
            // reach native code untracked by the GC.
            var valueType = t.IsByRef ? t.GetElementType() : t;
            if (valueType.IsValueType && !ClrLayout.IsBlittable(valueType))
            {
                throw new ArgumentException(
                    $"Internal call '{name}': struct {valueType} contains object references " +
                    "and cannot cross the native boundary by value");
            }
            // By-value structs get interop-marshalled by the runtime; the
            // layout only matches C++ when bool/char fields carry U1/U2
            // marshalling (see the ABI note above).
            if (!t.IsByRef && valueType.IsValueType && !HasNormalizedMarshalling(valueType))
            {
                throw new ArgumentException(
                    $"Internal call '{name}': struct {valueType} has bool/char fields without " +
                    "[MarshalAs(UnmanagedType.U1/U2)] - by value they would marshal as BOOL/ANSI " +
                    "and corrupt the raw CLR/C++ layout");
            }
            paramTypes[i] = t;
        }

        if (returnType.IsByRef || returnType.IsPointer)
        {
            throw new ArgumentException($"Internal call '{name}': unsupported return type {returnType}");
        }
        if (returnType != typeof(void) && returnType.IsValueType && !ClrLayout.IsBlittable(returnType))
        {
            throw new ArgumentException(
                $"Internal call '{name}': struct {returnType} contains object references " +
                "and cannot cross the native boundary by value");
        }
        if (returnType != typeof(void) && returnType.IsValueType && !HasNormalizedMarshalling(returnType))
        {
            throw new ArgumentException(
                $"Internal call '{name}': struct {returnType} has bool/char fields without " +
                "[MarshalAs(UnmanagedType.U1/U2)] - by value they would marshal as BOOL/ANSI " +
                "and corrupt the raw CLR/C++ layout");
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

    /// True when every bool/char field of the struct (recursively) carries
    /// [MarshalAs(U1/U2)], i.e. the interop-marshalled layout the runtime
    /// produces for a by-value crossing is byte-identical to the CLR/C++
    /// layout. The weaver annotates icall structs automatically; structs
    /// used through Bind at runtime need the attributes written by hand.
    private static bool HasNormalizedMarshalling(Type type)
    {
        if (type.IsPrimitive || type.IsEnum || type.IsPointer)
        {
            return true;
        }

        foreach (var field in type.GetFields(
                     BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic))
        {
            var fieldType = field.FieldType;
            if (fieldType == typeof(bool) || fieldType == typeof(char))
            {
                var expected = fieldType == typeof(bool) ? UnmanagedType.U1 : UnmanagedType.U2;
                var marshalAs = field.GetCustomAttribute<MarshalAsAttribute>();
                if (marshalAs == null || marshalAs.Value != expected)
                {
                    return false;
                }
            }
            else if (fieldType.IsValueType && !HasNormalizedMarshalling(fieldType))
            {
                return false;
            }
        }

        return true;
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
        var nativeParamTypes = new List<Type>();
        var marshalled = new LocalBuilder[paramTypes.Length];
        var release = new MethodInfo[paramTypes.Length];

        for (int i = 0; i < paramTypes.Length; i++)
        {
            var t = paramTypes[i];
            if (t == typeof(string))
            {
                marshalled[i] = il.DeclareLocal(typeof(IntPtr));
                release[i] = freeUtf8;
                il.Emit(OpCodes.Ldarg, (short)i);
                il.Emit(OpCodes.Call, allocUtf8);
                il.Emit(OpCodes.Stloc, marshalled[i]);
            }
            else if (t.IsByRef)
            {
                // By-ref value type: travels as a raw pointer to the struct,
                // matching mono's icall ABI for out/ref parameters. Pinned
                // for the duration of the call - the referent may live on
                // the GC heap (field, array element) and must not move while
                // native code holds the pointer.
                marshalled[i] = il.DeclareLocal(t, pinned: true);
                il.Emit(OpCodes.Ldarg, (short)i);
                il.Emit(OpCodes.Stloc, marshalled[i]);
            }
            else if (!t.IsValueType)
            {
                marshalled[i] = il.DeclareLocal(typeof(IntPtr));
                release[i] = freeHandle;
                il.Emit(OpCodes.Ldarg, (short)i);
                il.Emit(OpCodes.Call, allocHandle);
                il.Emit(OpCodes.Stloc, marshalled[i]);
            }
        }

        var nativeReturnType =
            returnType == typeof(void) ? typeof(void)
            : returnType == typeof(string) || !returnType.IsValueType ? typeof(IntPtr)
            : returnType == typeof(bool) || returnType == typeof(char) ? typeof(int)
            : returnType;

        // Push the arguments and the target pointer, then raw-call.
        for (int i = 0; i < paramTypes.Length; i++)
        {
            var t = paramTypes[i];
            if (t.IsByRef)
            {
                il.Emit(OpCodes.Ldloc, marshalled[i]);
                il.Emit(OpCodes.Conv_I);
                nativeParamTypes.Add(typeof(IntPtr));
            }
            else if (marshalled[i] != null)
            {
                il.Emit(OpCodes.Ldloc, marshalled[i]);
                nativeParamTypes.Add(typeof(IntPtr));
            }
            else if (t == typeof(bool) || t == typeof(char))
            {
                // Bools and chars travel as int32 (see icall_abi<bool> /
                // icall_abi<char16_t> on the native side): the default
                // interop treatment (4-byte BOOL / ANSI char) would diverge
                // from the raw CLR/C++ representation.
                il.Emit(OpCodes.Ldarg, (short)i);
                nativeParamTypes.Add(typeof(int));
            }
            else
            {
                // Scalars and structs by value. Struct layouts match C++
                // because bool/char fields carry U1/U2 marshalling
                // (validated in BindPointer).
                il.Emit(OpCodes.Ldarg, (short)i);
                nativeParamTypes.Add(t);
            }
        }

        il.Emit(OpCodes.Ldc_I8, fn.ToInt64());
        il.Emit(OpCodes.Conv_I);

        il.EmitCalli(OpCodes.Calli, CallingConvention.Winapi, nativeReturnType, nativeParamTypes.ToArray());

        LocalBuilder result = null;
        if (nativeReturnType != typeof(void))
        {
            result = il.DeclareLocal(nativeReturnType);
            il.Emit(OpCodes.Stloc, result);
        }

        for (int i = 0; i < paramTypes.Length; i++)
        {
            if (release[i] != null)
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
        else if (returnType == typeof(char))
        {
            // Truncate the int32 wire value back to a utf16 char.
            il.Emit(OpCodes.Ldloc, result);
            il.Emit(OpCodes.Conv_U2);
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
