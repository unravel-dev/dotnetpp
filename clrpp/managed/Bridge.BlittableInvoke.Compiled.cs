using System;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;

namespace Clrpp
{

/// <summary>
/// DynamicMethod blittable invokers (arity ≤ 8, static and instance).
/// Prefer this over Portable when IsDynamicCodeCompiled — mainly for instance
/// shapes (Portable already covers public static arity ≤ 8 via CreateDelegate).
/// Emit into typeof(Bridge).Module so thunks do not root collectible ALCs.
/// Instance `this` is passed through without IL Castclass — the call/callvirt
/// uses the MethodInfo directly (same tolerance as MethodInvoker across ALCs).
/// Gated by RuntimeFeature.IsDynamicCodeCompiled; failures fall through.
/// </summary>
public static partial class Bridge
{
    private static bool TryBuildCompiledBlittableCall(MethodInfo method, ParameterInfo[] parameters,
                                                      bool isVoid, out BlittableCall call)
    {
        call = null;
        if (method == null || parameters == null)
        {
            return false;
        }

        if (parameters.Length > MaxCompiledBlittableArity)
        {
            return false;
        }

        if (!method.IsStatic)
        {
            var declaring = method.DeclaringType;
            if (declaring == null)
            {
                return false;
            }
        }

        try
        {
            call = EmitCompiledBlittableCall(method, parameters, isVoid);
            return call != null;
        }
        catch (Exception ex)
        {
            Log($"Compiled blittable invoke unavailable for {method.DeclaringType}.{method.Name}: {ex.Message}",
                "trace");
            call = null;
            return false;
        }
    }

    private static BlittableCall EmitCompiledBlittableCall(MethodInfo method, ParameterInfo[] parameters,
                                                           bool isVoid)
    {
        var declaring = method.DeclaringType;
        var dm = new DynamicMethod(
            "clrpp_blit_" + method.MetadataToken.ToString("X"),
            typeof(void),
            new[] { typeof(object), typeof(NativeVariant*), typeof(NativeVariant*) },
            typeof(Bridge).Module,
            skipVisibility: true);
        var il = dm.GetILGenerator();

        if (!method.IsStatic)
        {
            il.Emit(OpCodes.Ldarg_0);
            if (declaring.IsValueType)
            {
                il.Emit(OpCodes.Unbox, declaring);
            }
            // Reference-type this: leave as object. Do not Castclass to
            // DeclaringType — that fails across collectible ALC boundaries
            // where MethodInvoker still succeeds.
        }

        for (int i = 0; i < parameters.Length; i++)
        {
            var read = typeof(Bridge)
                .GetMethod(nameof(CompiledBlittableReadArg),
                           BindingFlags.NonPublic | BindingFlags.Static)
                .MakeGenericMethod(parameters[i].ParameterType);
            il.Emit(OpCodes.Ldarg_1);
            il.Emit(OpCodes.Ldc_I4, i);
            il.Emit(OpCodes.Call, read);
        }

        if (method.IsStatic)
        {
            il.Emit(OpCodes.Call, method);
        }
        else if (declaring.IsValueType)
        {
            il.Emit(OpCodes.Call, method);
        }
        else if (method.IsVirtual && !method.IsFinal)
        {
            il.Emit(OpCodes.Callvirt, method);
        }
        else
        {
            // Non-virtual / private instance — call with object this
            // (skipVisibility DynamicMethod), matching MethodInvoker.
            il.Emit(OpCodes.Call, method);
        }

        if (!isVoid)
        {
            var retLocal = il.DeclareLocal(method.ReturnType);
            il.Emit(OpCodes.Stloc, retLocal);
            var write = typeof(Bridge)
                .GetMethod(nameof(CompiledBlittableWriteRet),
                           BindingFlags.NonPublic | BindingFlags.Static)
                .MakeGenericMethod(method.ReturnType);
            il.Emit(OpCodes.Ldarg_2);
            il.Emit(OpCodes.Ldloc, retLocal);
            il.Emit(OpCodes.Call, write);
        }

        il.Emit(OpCodes.Ret);
        return dm.CreateDelegate<BlittableCall>();
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static unsafe T CompiledBlittableReadArg<T>(NativeVariant* args, int index)
        where T : unmanaged
    {
        return Unsafe.ReadUnaligned<T>((void*)args[index].Data);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static unsafe void CompiledBlittableWriteRet<T>(NativeVariant* result, T value)
        where T : unmanaged
    {
        // Caller discards the return value (e.g. a non-void method bound to a
        // C++ void thunk, which passes a kind_empty result). Match the portable
        // path: the method already ran, so just drop the value.
        if (result != null && result->Kind == NativeVariant.KindEmpty)
        {
            return;
        }

        if (result == null || result->Kind != NativeVariant.KindBlob || result->Data == IntPtr.Zero)
        {
            throw new ArgumentException("Blittable invoke requires a blob result buffer");
        }

        var size = Unsafe.SizeOf<T>();
        if (size > result->Size)
        {
            throw new ArgumentException(
                $"Return value of type {typeof(T)} does not fit into {result->Size} bytes");
        }

        Unsafe.WriteUnaligned((void*)result->Data, value);
        result->Size = size;
    }
}

} // namespace Clrpp
