using System;
using System.Reflection;
using System.Runtime.CompilerServices;

namespace Clrpp
{

/// <summary>
/// Blittable N→M invoke binders.
/// Portable (this file): CreateDelegate for public static methods, arity ≤ 8
/// (interpreter-safe, no emit).
/// Compiled (Bridge.BlittableInvoke.Compiled.cs): DynamicMethod for instance
/// (and static fallback) when IsDynamicCodeCompiled.
/// Instance open-delegates are never used — they cast to DeclaringType and
/// can fail across collectible ALC boundaries.
/// </summary>
public static partial class Bridge
{
    private const int MaxPortableBlittableArity = 8;
    private const int MaxCompiledBlittableArity = 8;

    private unsafe delegate void BlittableCall(object target, NativeVariant* args, NativeVariant* result);

    private static bool IsBlittableInvokeType(Type type)
    {
        return type != null && type != typeof(void) && ClrLayout.IsBlittable(type);
    }

    private static bool TryBuildBlittableCall(MethodInfo method, ParameterInfo[] parameters,
                                              out BlittableCall call)
    {
        call = null;
        if (!IsEligibleBlittableInvoke(method, parameters, out var isVoid))
        {
            return false;
        }

        // Prefer Compiled when available (covers instance + arity up to 8).
        if (CanCompileDynamicCode &&
            TryBuildCompiledBlittableCall(method, parameters, isVoid, out call))
        {
            return true;
        }

        // Portable: public static only, arity ≤ 8.
        if (TryBuildPortableBlittableCall(method, parameters, isVoid, out call))
        {
            return true;
        }

        call = null;
        return false;
    }

    private static bool IsEligibleBlittableInvoke(MethodInfo method, ParameterInfo[] parameters,
                                                  out bool isVoid)
    {
        isVoid = false;
        if (method == null || parameters == null)
        {
            return false;
        }

        if (parameters.Length > MaxCompiledBlittableArity)
        {
            return false;
        }

        for (int i = 0; i < parameters.Length; i++)
        {
            var p = parameters[i].ParameterType;
            if (p.IsByRef || !IsBlittableInvokeType(p))
            {
                return false;
            }
        }

        var ret = method.ReturnType;
        isVoid = ret == typeof(void);
        if (!isVoid && !IsBlittableInvokeType(ret))
        {
            return false;
        }

        if (!method.IsStatic && method.DeclaringType == null)
        {
            return false;
        }

        return true;
    }

    private static bool TryBuildPortableBlittableCall(MethodInfo method, ParameterInfo[] parameters,
                                                      bool isVoid, out BlittableCall call)
    {
        call = null;

        // No instance CreateDelegate path — DeclaringType casts break across
        // collectible ALCs. Instance shapes use Compiled or MethodInvoker.
        if (!method.IsStatic || !method.IsPublic)
        {
            return false;
        }

        if (parameters.Length > MaxPortableBlittableArity)
        {
            return false;
        }

        try
        {
            call = BindPortableStaticBlittableCall(method, parameters, isVoid);
            return call != null;
        }
        catch
        {
            call = null;
            return false;
        }
    }

    private static BlittableCall BindPortableStaticBlittableCall(MethodInfo method,
                                                                  ParameterInfo[] parameters,
                                                                  bool isVoid)
    {
        var argc = parameters.Length;
        return isVoid
            ? BindStaticVoid(method, parameters, argc)
            : BindStaticRet(method, parameters, argc);
    }

    private static Type[] GenericArgs(ParameterInfo[] parameters, int argc, Type ret = null)
    {
        var n = ret != null ? argc + 1 : argc;
        var args = new Type[n];
        for (int i = 0; i < argc; i++)
        {
            args[i] = parameters[i].ParameterType;
        }

        if (ret != null)
        {
            args[argc] = ret;
        }

        return args;
    }

    private static BlittableCall BindStaticVoid(MethodInfo method, ParameterInfo[] parameters, int argc)
    {
        return argc switch
        {
            0 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.StaticAction0))
                .Invoke(null, new object[] { method }),
            1 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.StaticAction1))
                .MakeGenericMethod(GenericArgs(parameters, 1))
                .Invoke(null, new object[] { method }),
            2 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.StaticAction2))
                .MakeGenericMethod(GenericArgs(parameters, 2))
                .Invoke(null, new object[] { method }),
            3 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.StaticAction3))
                .MakeGenericMethod(GenericArgs(parameters, 3))
                .Invoke(null, new object[] { method }),
            4 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.StaticAction4))
                .MakeGenericMethod(GenericArgs(parameters, 4))
                .Invoke(null, new object[] { method }),
            5 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.StaticAction5))
                .MakeGenericMethod(GenericArgs(parameters, 5))
                .Invoke(null, new object[] { method }),
            6 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.StaticAction6))
                .MakeGenericMethod(GenericArgs(parameters, 6))
                .Invoke(null, new object[] { method }),
            7 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.StaticAction7))
                .MakeGenericMethod(GenericArgs(parameters, 7))
                .Invoke(null, new object[] { method }),
            8 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.StaticAction8))
                .MakeGenericMethod(GenericArgs(parameters, 8))
                .Invoke(null, new object[] { method }),
            _ => null,
        };
    }

    private static BlittableCall BindStaticRet(MethodInfo method, ParameterInfo[] parameters, int argc)
    {
        var ret = method.ReturnType;
        return argc switch
        {
            0 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.StaticFunc0))
                .MakeGenericMethod(ret)
                .Invoke(null, new object[] { method }),
            1 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.StaticFunc1))
                .MakeGenericMethod(GenericArgs(parameters, 1, ret))
                .Invoke(null, new object[] { method }),
            2 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.StaticFunc2))
                .MakeGenericMethod(GenericArgs(parameters, 2, ret))
                .Invoke(null, new object[] { method }),
            3 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.StaticFunc3))
                .MakeGenericMethod(GenericArgs(parameters, 3, ret))
                .Invoke(null, new object[] { method }),
            4 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.StaticFunc4))
                .MakeGenericMethod(GenericArgs(parameters, 4, ret))
                .Invoke(null, new object[] { method }),
            5 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.StaticFunc5))
                .MakeGenericMethod(GenericArgs(parameters, 5, ret))
                .Invoke(null, new object[] { method }),
            6 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.StaticFunc6))
                .MakeGenericMethod(GenericArgs(parameters, 6, ret))
                .Invoke(null, new object[] { method }),
            7 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.StaticFunc7))
                .MakeGenericMethod(GenericArgs(parameters, 7, ret))
                .Invoke(null, new object[] { method }),
            8 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.StaticFunc8))
                .MakeGenericMethod(GenericArgs(parameters, 8, ret))
                .Invoke(null, new object[] { method }),
            _ => null,
        };
    }

    private static unsafe bool ArgsAreBlobs(NativeVariant* args, int argc)
    {
        for (int i = 0; i < argc; i++)
        {
            if (args[i].Kind != NativeVariant.KindBlob || args[i].Data == IntPtr.Zero)
            {
                return false;
            }
        }

        return true;
    }

    private static unsafe class BlittableBind
    {
        private static T Read<T>(NativeVariant* args, int index) where T : unmanaged
        {
            return Unsafe.ReadUnaligned<T>((void*)args[index].Data);
        }

        private static void Write<T>(NativeVariant* result, T value) where T : unmanaged
        {
            if (result->Kind != NativeVariant.KindBlob || result->Data == IntPtr.Zero)
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

        public static BlittableCall StaticAction0(MethodInfo method)
        {
            var d = (Action)method.CreateDelegate(typeof(Action));
            return (target, args, result) => d();
        }

        public static BlittableCall StaticAction1<T1>(MethodInfo method) where T1 : unmanaged
        {
            var d = (Action<T1>)method.CreateDelegate(typeof(Action<T1>));
            return (target, args, result) => d(Read<T1>(args, 0));
        }

        public static BlittableCall StaticAction2<T1, T2>(MethodInfo method)
            where T1 : unmanaged where T2 : unmanaged
        {
            var d = (Action<T1, T2>)method.CreateDelegate(typeof(Action<T1, T2>));
            return (target, args, result) => d(Read<T1>(args, 0), Read<T2>(args, 1));
        }

        public static BlittableCall StaticAction3<T1, T2, T3>(MethodInfo method)
            where T1 : unmanaged where T2 : unmanaged where T3 : unmanaged
        {
            var d = (Action<T1, T2, T3>)method.CreateDelegate(typeof(Action<T1, T2, T3>));
            return (target, args, result) => d(Read<T1>(args, 0), Read<T2>(args, 1), Read<T3>(args, 2));
        }

        public static BlittableCall StaticAction4<T1, T2, T3, T4>(MethodInfo method)
            where T1 : unmanaged where T2 : unmanaged where T3 : unmanaged where T4 : unmanaged
        {
            var d = (Action<T1, T2, T3, T4>)method.CreateDelegate(typeof(Action<T1, T2, T3, T4>));
            return (target, args, result) =>
                d(Read<T1>(args, 0), Read<T2>(args, 1), Read<T3>(args, 2), Read<T4>(args, 3));
        }

        public static BlittableCall StaticAction5<T1, T2, T3, T4, T5>(MethodInfo method)
            where T1 : unmanaged where T2 : unmanaged where T3 : unmanaged
            where T4 : unmanaged where T5 : unmanaged
        {
            var d = (Action<T1, T2, T3, T4, T5>)method.CreateDelegate(
                typeof(Action<T1, T2, T3, T4, T5>));
            return (target, args, result) =>
                d(Read<T1>(args, 0), Read<T2>(args, 1), Read<T3>(args, 2), Read<T4>(args, 3),
                  Read<T5>(args, 4));
        }

        public static BlittableCall StaticAction6<T1, T2, T3, T4, T5, T6>(MethodInfo method)
            where T1 : unmanaged where T2 : unmanaged where T3 : unmanaged
            where T4 : unmanaged where T5 : unmanaged where T6 : unmanaged
        {
            var d = (Action<T1, T2, T3, T4, T5, T6>)method.CreateDelegate(
                typeof(Action<T1, T2, T3, T4, T5, T6>));
            return (target, args, result) =>
                d(Read<T1>(args, 0), Read<T2>(args, 1), Read<T3>(args, 2), Read<T4>(args, 3),
                  Read<T5>(args, 4), Read<T6>(args, 5));
        }

        public static BlittableCall StaticAction7<T1, T2, T3, T4, T5, T6, T7>(MethodInfo method)
            where T1 : unmanaged where T2 : unmanaged where T3 : unmanaged where T4 : unmanaged
            where T5 : unmanaged where T6 : unmanaged where T7 : unmanaged
        {
            var d = (Action<T1, T2, T3, T4, T5, T6, T7>)method.CreateDelegate(
                typeof(Action<T1, T2, T3, T4, T5, T6, T7>));
            return (target, args, result) =>
                d(Read<T1>(args, 0), Read<T2>(args, 1), Read<T3>(args, 2), Read<T4>(args, 3),
                  Read<T5>(args, 4), Read<T6>(args, 5), Read<T7>(args, 6));
        }

        public static BlittableCall StaticAction8<T1, T2, T3, T4, T5, T6, T7, T8>(MethodInfo method)
            where T1 : unmanaged where T2 : unmanaged where T3 : unmanaged where T4 : unmanaged
            where T5 : unmanaged where T6 : unmanaged where T7 : unmanaged where T8 : unmanaged
        {
            var d = (Action<T1, T2, T3, T4, T5, T6, T7, T8>)method.CreateDelegate(
                typeof(Action<T1, T2, T3, T4, T5, T6, T7, T8>));
            return (target, args, result) =>
                d(Read<T1>(args, 0), Read<T2>(args, 1), Read<T3>(args, 2), Read<T4>(args, 3),
                  Read<T5>(args, 4), Read<T6>(args, 5), Read<T7>(args, 6), Read<T8>(args, 7));
        }

        public static BlittableCall StaticFunc0<TRet>(MethodInfo method) where TRet : unmanaged
        {
            var d = (Func<TRet>)method.CreateDelegate(typeof(Func<TRet>));
            return (target, args, result) => Write(result, d());
        }

        public static BlittableCall StaticFunc1<T1, TRet>(MethodInfo method)
            where T1 : unmanaged where TRet : unmanaged
        {
            var d = (Func<T1, TRet>)method.CreateDelegate(typeof(Func<T1, TRet>));
            return (target, args, result) => Write(result, d(Read<T1>(args, 0)));
        }

        public static BlittableCall StaticFunc2<T1, T2, TRet>(MethodInfo method)
            where T1 : unmanaged where T2 : unmanaged where TRet : unmanaged
        {
            var d = (Func<T1, T2, TRet>)method.CreateDelegate(typeof(Func<T1, T2, TRet>));
            return (target, args, result) => Write(result, d(Read<T1>(args, 0), Read<T2>(args, 1)));
        }

        public static BlittableCall StaticFunc3<T1, T2, T3, TRet>(MethodInfo method)
            where T1 : unmanaged where T2 : unmanaged where T3 : unmanaged where TRet : unmanaged
        {
            var d = (Func<T1, T2, T3, TRet>)method.CreateDelegate(typeof(Func<T1, T2, T3, TRet>));
            return (target, args, result) =>
                Write(result, d(Read<T1>(args, 0), Read<T2>(args, 1), Read<T3>(args, 2)));
        }

        public static BlittableCall StaticFunc4<T1, T2, T3, T4, TRet>(MethodInfo method)
            where T1 : unmanaged where T2 : unmanaged where T3 : unmanaged
            where T4 : unmanaged where TRet : unmanaged
        {
            var d = (Func<T1, T2, T3, T4, TRet>)method.CreateDelegate(
                typeof(Func<T1, T2, T3, T4, TRet>));
            return (target, args, result) =>
                Write(result, d(Read<T1>(args, 0), Read<T2>(args, 1), Read<T3>(args, 2),
                               Read<T4>(args, 3)));
        }

        public static BlittableCall StaticFunc5<T1, T2, T3, T4, T5, TRet>(MethodInfo method)
            where T1 : unmanaged where T2 : unmanaged where T3 : unmanaged where T4 : unmanaged
            where T5 : unmanaged where TRet : unmanaged
        {
            var d = (Func<T1, T2, T3, T4, T5, TRet>)method.CreateDelegate(
                typeof(Func<T1, T2, T3, T4, T5, TRet>));
            return (target, args, result) =>
                Write(result, d(Read<T1>(args, 0), Read<T2>(args, 1), Read<T3>(args, 2),
                               Read<T4>(args, 3), Read<T5>(args, 4)));
        }

        public static BlittableCall StaticFunc6<T1, T2, T3, T4, T5, T6, TRet>(MethodInfo method)
            where T1 : unmanaged where T2 : unmanaged where T3 : unmanaged where T4 : unmanaged
            where T5 : unmanaged where T6 : unmanaged where TRet : unmanaged
        {
            var d = (Func<T1, T2, T3, T4, T5, T6, TRet>)method.CreateDelegate(
                typeof(Func<T1, T2, T3, T4, T5, T6, TRet>));
            return (target, args, result) =>
                Write(result, d(Read<T1>(args, 0), Read<T2>(args, 1), Read<T3>(args, 2),
                               Read<T4>(args, 3), Read<T5>(args, 4), Read<T6>(args, 5)));
        }

        public static BlittableCall StaticFunc7<T1, T2, T3, T4, T5, T6, T7, TRet>(MethodInfo method)
            where T1 : unmanaged where T2 : unmanaged where T3 : unmanaged where T4 : unmanaged
            where T5 : unmanaged where T6 : unmanaged where T7 : unmanaged where TRet : unmanaged
        {
            var d = (Func<T1, T2, T3, T4, T5, T6, T7, TRet>)method.CreateDelegate(
                typeof(Func<T1, T2, T3, T4, T5, T6, T7, TRet>));
            return (target, args, result) =>
                Write(result, d(Read<T1>(args, 0), Read<T2>(args, 1), Read<T3>(args, 2),
                               Read<T4>(args, 3), Read<T5>(args, 4), Read<T6>(args, 5),
                               Read<T7>(args, 6)));
        }

        public static BlittableCall StaticFunc8<T1, T2, T3, T4, T5, T6, T7, T8, TRet>(MethodInfo method)
            where T1 : unmanaged where T2 : unmanaged where T3 : unmanaged where T4 : unmanaged
            where T5 : unmanaged where T6 : unmanaged where T7 : unmanaged where T8 : unmanaged
            where TRet : unmanaged
        {
            var d = (Func<T1, T2, T3, T4, T5, T6, T7, T8, TRet>)method.CreateDelegate(
                typeof(Func<T1, T2, T3, T4, T5, T6, T7, T8, TRet>));
            return (target, args, result) =>
                Write(result, d(Read<T1>(args, 0), Read<T2>(args, 1), Read<T3>(args, 2),
                               Read<T4>(args, 3), Read<T5>(args, 4), Read<T6>(args, 5),
                               Read<T7>(args, 6), Read<T8>(args, 7)));
        }
    }
}

} // namespace Clrpp
