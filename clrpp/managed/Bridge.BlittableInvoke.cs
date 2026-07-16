using System;
using System.Reflection;
using System.Runtime.CompilerServices;

namespace Clrpp
{

/// <summary>
/// Typed CreateDelegate invokers for blittable-only signatures. Avoids
/// object[] boxing and MethodInvoker/MethodBase.Invoke on the N→M hot path.
/// Uses existing method IL only - no DynamicMethod / Expression.Compile -
/// so it stays valid under the interpreter.
/// </summary>
public static partial class Bridge
{
    private unsafe delegate void BlittableCall(object target, NativeVariant* args, NativeVariant* result);

    private static bool IsBlittableInvokeType(Type type)
    {
        return type != null && type != typeof(void) && ClrLayout.IsBlittable(type);
    }

    private static bool TryBuildBlittableCall(MethodInfo method, ParameterInfo[] parameters,
                                              out BlittableCall call)
    {
        call = null;
        if (method == null || parameters == null)
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
        var isVoid = ret == typeof(void);
        if (!isVoid && !IsBlittableInvokeType(ret))
        {
            return false;
        }

        // Open instance delegates need a reference-type `this`.
        if (!method.IsStatic)
        {
            var declaring = method.DeclaringType;
            if (declaring == null || declaring.IsValueType)
            {
                return false;
            }
        }

        try
        {
            call = BindBlittableCall(method, parameters, isVoid);
            return call != null;
        }
        catch
        {
            call = null;
            return false;
        }
    }

    private static BlittableCall BindBlittableCall(MethodInfo method, ParameterInfo[] parameters,
                                                   bool isVoid)
    {
        var argc = parameters.Length;
        if (method.IsStatic)
        {
            return isVoid
                ? BindStaticVoid(method, parameters, argc)
                : BindStaticRet(method, parameters, argc);
        }

        return isVoid
            ? BindInstanceVoid(method, parameters, argc)
            : BindInstanceRet(method, parameters, argc);
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
                .MakeGenericMethod(parameters[0].ParameterType)
                .Invoke(null, new object[] { method }),
            2 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.StaticAction2))
                .MakeGenericMethod(parameters[0].ParameterType, parameters[1].ParameterType)
                .Invoke(null, new object[] { method }),
            3 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.StaticAction3))
                .MakeGenericMethod(parameters[0].ParameterType, parameters[1].ParameterType,
                                   parameters[2].ParameterType)
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
                .MakeGenericMethod(parameters[0].ParameterType, ret)
                .Invoke(null, new object[] { method }),
            2 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.StaticFunc2))
                .MakeGenericMethod(parameters[0].ParameterType, parameters[1].ParameterType, ret)
                .Invoke(null, new object[] { method }),
            3 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.StaticFunc3))
                .MakeGenericMethod(parameters[0].ParameterType, parameters[1].ParameterType,
                                   parameters[2].ParameterType, ret)
                .Invoke(null, new object[] { method }),
            _ => null,
        };
    }

    private static BlittableCall BindInstanceVoid(MethodInfo method, ParameterInfo[] parameters, int argc)
    {
        var targetType = method.DeclaringType;
        return argc switch
        {
            0 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.InstanceAction0))
                .MakeGenericMethod(targetType)
                .Invoke(null, new object[] { method }),
            1 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.InstanceAction1))
                .MakeGenericMethod(targetType, parameters[0].ParameterType)
                .Invoke(null, new object[] { method }),
            2 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.InstanceAction2))
                .MakeGenericMethod(targetType, parameters[0].ParameterType, parameters[1].ParameterType)
                .Invoke(null, new object[] { method }),
            3 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.InstanceAction3))
                .MakeGenericMethod(targetType, parameters[0].ParameterType, parameters[1].ParameterType,
                                   parameters[2].ParameterType)
                .Invoke(null, new object[] { method }),
            _ => null,
        };
    }

    private static BlittableCall BindInstanceRet(MethodInfo method, ParameterInfo[] parameters, int argc)
    {
        var targetType = method.DeclaringType;
        var ret = method.ReturnType;
        return argc switch
        {
            0 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.InstanceFunc0))
                .MakeGenericMethod(targetType, ret)
                .Invoke(null, new object[] { method }),
            1 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.InstanceFunc1))
                .MakeGenericMethod(targetType, parameters[0].ParameterType, ret)
                .Invoke(null, new object[] { method }),
            2 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.InstanceFunc2))
                .MakeGenericMethod(targetType, parameters[0].ParameterType, parameters[1].ParameterType, ret)
                .Invoke(null, new object[] { method }),
            3 => (BlittableCall)typeof(BlittableBind)
                .GetMethod(nameof(BlittableBind.InstanceFunc3))
                .MakeGenericMethod(targetType, parameters[0].ParameterType, parameters[1].ParameterType,
                                   parameters[2].ParameterType, ret)
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

        public static BlittableCall InstanceAction0<TTarget>(MethodInfo method) where TTarget : class
        {
            var d = (Action<TTarget>)method.CreateDelegate(typeof(Action<TTarget>));
            return (target, args, result) => d((TTarget)target);
        }

        public static BlittableCall InstanceAction1<TTarget, T1>(MethodInfo method)
            where TTarget : class where T1 : unmanaged
        {
            var d = (Action<TTarget, T1>)method.CreateDelegate(typeof(Action<TTarget, T1>));
            return (target, args, result) => d((TTarget)target, Read<T1>(args, 0));
        }

        public static BlittableCall InstanceAction2<TTarget, T1, T2>(MethodInfo method)
            where TTarget : class where T1 : unmanaged where T2 : unmanaged
        {
            var d = (Action<TTarget, T1, T2>)method.CreateDelegate(typeof(Action<TTarget, T1, T2>));
            return (target, args, result) => d((TTarget)target, Read<T1>(args, 0), Read<T2>(args, 1));
        }

        public static BlittableCall InstanceAction3<TTarget, T1, T2, T3>(MethodInfo method)
            where TTarget : class where T1 : unmanaged where T2 : unmanaged where T3 : unmanaged
        {
            var d = (Action<TTarget, T1, T2, T3>)method.CreateDelegate(typeof(Action<TTarget, T1, T2, T3>));
            return (target, args, result) =>
                d((TTarget)target, Read<T1>(args, 0), Read<T2>(args, 1), Read<T3>(args, 2));
        }

        public static BlittableCall InstanceFunc0<TTarget, TRet>(MethodInfo method)
            where TTarget : class where TRet : unmanaged
        {
            var d = (Func<TTarget, TRet>)method.CreateDelegate(typeof(Func<TTarget, TRet>));
            return (target, args, result) => Write(result, d((TTarget)target));
        }

        public static BlittableCall InstanceFunc1<TTarget, T1, TRet>(MethodInfo method)
            where TTarget : class where T1 : unmanaged where TRet : unmanaged
        {
            var d = (Func<TTarget, T1, TRet>)method.CreateDelegate(typeof(Func<TTarget, T1, TRet>));
            return (target, args, result) => Write(result, d((TTarget)target, Read<T1>(args, 0)));
        }

        public static BlittableCall InstanceFunc2<TTarget, T1, T2, TRet>(MethodInfo method)
            where TTarget : class where T1 : unmanaged where T2 : unmanaged where TRet : unmanaged
        {
            var d = (Func<TTarget, T1, T2, TRet>)method.CreateDelegate(typeof(Func<TTarget, T1, T2, TRet>));
            return (target, args, result) =>
                Write(result, d((TTarget)target, Read<T1>(args, 0), Read<T2>(args, 1)));
        }

        public static BlittableCall InstanceFunc3<TTarget, T1, T2, T3, TRet>(MethodInfo method)
            where TTarget : class
            where T1 : unmanaged
            where T2 : unmanaged
            where T3 : unmanaged
            where TRet : unmanaged
        {
            var d = (Func<TTarget, T1, T2, T3, TRet>)method.CreateDelegate(
                typeof(Func<TTarget, T1, T2, T3, TRet>));
            return (target, args, result) =>
                Write(result, d((TTarget)target, Read<T1>(args, 0), Read<T2>(args, 1), Read<T3>(args, 2)));
        }
    }
}

} // namespace Clrpp
