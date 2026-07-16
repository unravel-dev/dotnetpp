using System;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Clrpp
{

/// <summary>
/// Hot-path caches for bridge invoke/array/field operations. All caches
/// keep managed objects alive only through ordinary GCHandles or weak tables;
/// no raw managed pointers escape to native code.
/// </summary>
public static partial class Bridge
{
    private const int MaxCachedInvokeArity = 8;

    // ---------------------------------------------------------------------
    // Array pin cache (thread-local, reused across sequential element copies)
    // ---------------------------------------------------------------------

    private static class ArrayPinCache
    {
        [ThreadStatic] private static IntPtr s_handle;
        [ThreadStatic] private static GCHandle s_pin;
        [ThreadStatic] private static int s_elementSize;
        [ThreadStatic] private static long s_byteLength;

        internal static GCHandle Pin(Array array, IntPtr arrayHandle)
        {
            if (s_handle == arrayHandle && s_pin.IsAllocated)
            {
                return s_pin;
            }

            ReleaseCurrent();
            s_pin = GCHandle.Alloc(array, GCHandleType.Pinned);
            s_handle = arrayHandle;
            s_elementSize = 0;
            s_byteLength = -1;
            return s_pin;
        }

        internal static int ElementSize(Array array)
        {
            if (s_elementSize > 0)
            {
                return s_elementSize;
            }

            s_elementSize = ClrLayout.SizeOf(array.GetType().GetElementType());
            return s_elementSize;
        }

        internal static long ByteLength(Array array)
        {
            if (s_byteLength >= 0)
            {
                return s_byteLength;
            }

            s_byteLength = array.LongLength * ElementSize(array);
            return s_byteLength;
        }

        internal static void Release(IntPtr arrayHandle)
        {
            if (s_handle == arrayHandle)
            {
                ReleaseCurrent();
            }
        }

        internal static void ReleaseCurrent()
        {
            if (s_pin.IsAllocated)
            {
                s_pin.Free();
            }

            s_handle = IntPtr.Zero;
            s_elementSize = 0;
            s_byteLength = -1;
        }
    }

    // ---------------------------------------------------------------------
    // Method invoke cache (MethodInvoker + exact-arity args buffers)
    // ---------------------------------------------------------------------

    private sealed class MethodInvokePlan
    {
        public MethodBase Method;
        public ParameterInfo[] Parameters;
        public MethodInvoker MethodInvoker;
        public ConstructorInvoker ConstructorInvoker;
        public BlittableCall BlittableInvoke;
        public int BlittableArgc = -1;
    }

    private static readonly ConditionalWeakTable<MethodBase, MethodInvokePlan> MethodPlans = new();

    // Exact-length pools: MethodBase.Invoke / MethodInvoker require argc == Length.
    [ThreadStatic] private static object[][] s_invokeArgsByArity;

    private static MethodInvokePlan GetMethodPlan(IntPtr methodHandle)
    {
        var method = Target<MethodBase>(methodHandle);
        if (method == null)
        {
            return new MethodInvokePlan();
        }

        return MethodPlans.GetValue(method, static m => CreateMethodPlan(m));
    }

    private static MethodInvokePlan CreateMethodPlan(MethodBase method)
    {
        var plan = new MethodInvokePlan
        {
            Method = method,
            Parameters = method.GetParameters(),
        };

        try
        {
            if (method is ConstructorInfo ctor)
            {
                plan.ConstructorInvoker = ConstructorInvoker.Create(ctor);
            }
            else if (method is MethodInfo mi)
            {
                plan.MethodInvoker = MethodInvoker.Create(mi);
                if (TryBuildBlittableCall(mi, plan.Parameters, out var blittable))
                {
                    plan.BlittableInvoke = blittable;
                    plan.BlittableArgc = plan.Parameters.Length;
                }
            }
        }
        catch (Exception ex)
        {
            // Keep Method.Invoke fallback; never fail plan creation.
            Log($"MethodInvoker cache unavailable for {method}: {ex.Message}", "warning");
        }

        return plan;
    }

    private static object InvokeWithPlan(MethodInvokePlan plan, object target, object[] managedArgs)
    {
        // Constructor reinit on an existing instance has no ConstructorInvoker path.
        if (plan.Method is ConstructorInfo ctor && target != null)
        {
            return ctor.Invoke(target, managedArgs);
        }

        if (plan.ConstructorInvoker != null)
        {
            if (managedArgs == null || managedArgs.Length == 0)
            {
                return plan.ConstructorInvoker.Invoke();
            }

            return plan.ConstructorInvoker.Invoke(managedArgs.AsSpan());
        }

        if (plan.MethodInvoker != null)
        {
            if (managedArgs == null || managedArgs.Length == 0)
            {
                return plan.MethodInvoker.Invoke(target);
            }

            return plan.MethodInvoker.Invoke(target, managedArgs.AsSpan());
        }

        return plan.Method.Invoke(target, managedArgs);
    }

    private static object[] RentInvokeArgs(int argc)
    {
        if (argc <= 0)
        {
            return null;
        }

        var pools = s_invokeArgsByArity;
        if (pools == null)
        {
            pools = new object[MaxCachedInvokeArity + 1][];
            s_invokeArgsByArity = pools;
        }

        object[] buffer;
        if (argc <= MaxCachedInvokeArity)
        {
            buffer = pools[argc];
            if (buffer == null)
            {
                buffer = new object[argc];
                pools[argc] = buffer;
            }
        }
        else
        {
            // Rare high-arity calls: keep a single oversized slot and grow as needed.
            buffer = pools[0];
            if (buffer == null || buffer.Length != argc)
            {
                buffer = new object[argc];
                pools[0] = buffer;
            }
        }

        Array.Clear(buffer, 0, argc);
        return buffer;
    }

    private static void ReturnInvokeArgs(object[] buffer, int argc)
    {
        if (buffer == null || argc <= 0)
        {
            return;
        }

        Array.Clear(buffer, 0, argc);
    }

    // ---------------------------------------------------------------------
    // Field access cache (offset copy when possible; no Expression.Compile)
    // ---------------------------------------------------------------------

    private sealed class FieldAccessPlan
    {
        public FieldInfo Field;
        public bool IsBlittable;
        public int Size;
        public int Offset;
        public bool HasOffset;
    }

    private static readonly ConditionalWeakTable<FieldInfo, FieldAccessPlan> FieldPlans = new();

    private static FieldAccessPlan GetFieldPlan(FieldInfo field)
    {
        return FieldPlans.GetValue(field, static f =>
        {
            var plan = new FieldAccessPlan { Field = f };
            if (!f.FieldType.IsValueType || !ClrLayout.IsBlittable(f.FieldType))
            {
                return plan;
            }

            plan.IsBlittable = true;
            plan.Size = ClrLayout.SizeOf(f.FieldType);
            if (!f.IsStatic && TryGetInstanceFieldOffset(f, out var offset))
            {
                plan.HasOffset = true;
                plan.Offset = offset;
            }

            return plan;
        });
    }

    private static bool TryGetInstanceFieldOffset(FieldInfo field, out int offset)
    {
        offset = 0;
        var declaring = field.DeclaringType;
        if (declaring == null || !declaring.IsValueType)
        {
            return false;
        }

        try
        {
            offset = (int)Marshal.OffsetOf(declaring, field.Name);
            return true;
        }
        catch
        {
            return false;
        }
    }

    private static unsafe bool TryFieldGetBlittable(FieldInfo field, object target, ref NativeVariant result,
                                             NativeExceptionInfo* exInfo)
    {
        if (result.Kind != NativeVariant.KindBlob)
        {
            return false;
        }

        var plan = GetFieldPlan(field);
        if (!plan.IsBlittable)
        {
            return false;
        }

        try
        {
            if (plan.HasOffset && target != null && field.DeclaringType.IsInstanceOfType(target))
            {
                var pin = GCHandle.Alloc(target, GCHandleType.Pinned);
                try
                {
                    if (plan.Size > result.Size)
                    {
                        throw new ArgumentException(
                            $"Field {field.Name} value does not fit into {result.Size} bytes");
                    }

                    Buffer.MemoryCopy(
                        (byte*)pin.AddrOfPinnedObject() + plan.Offset,
                        (void*)result.Data,
                        result.Size,
                        plan.Size);
                    result.Size = plan.Size;
                    return true;
                }
                finally
                {
                    pin.Free();
                }
            }

            var value = field.GetValue(target);
            var written = ClrLayout.Write(value, result.Data, result.Size);
            if (written < 0)
            {
                throw new ArgumentException(
                    $"Field {field.Name} value does not fit into {result.Size} bytes");
            }

            result.Size = written;
            return true;
        }
        catch (Exception ex)
        {
            FillException(ex, ref *exInfo);
            return true;
        }
    }

    private static unsafe bool TryFieldSetBlittable(FieldInfo field, object target, in NativeVariant value,
                                             NativeExceptionInfo* exInfo)
    {
        var plan = GetFieldPlan(field);
        if (!plan.IsBlittable || field.IsInitOnly)
        {
            return false;
        }

        try
        {
            if (plan.HasOffset && target != null && field.DeclaringType.IsInstanceOfType(target))
            {
                if (value.Size > 0 && value.Size < plan.Size)
                {
                    throw new ArgumentException(
                        $"Blob of {value.Size} bytes is too small for {field.FieldType} ({plan.Size} bytes)");
                }

                var pin = GCHandle.Alloc(target, GCHandleType.Pinned);
                try
                {
                    Buffer.MemoryCopy(
                        (void*)value.Data,
                        (byte*)pin.AddrOfPinnedObject() + plan.Offset,
                        plan.Size,
                        plan.Size);
                    return true;
                }
                finally
                {
                    pin.Free();
                }
            }

            var managedValue = ClrLayout.Read(field.FieldType, value.Data);
            field.SetValue(target, managedValue);
            return true;
        }
        catch (Exception ex)
        {
            FillException(ex, ref *exInfo);
            return true;
        }
    }
}

} // namespace Clrpp
