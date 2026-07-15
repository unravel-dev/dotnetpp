using System;
using System.Linq.Expressions;
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
    // ---------------------------------------------------------------------
    // Array pin cache (thread-local, reused across sequential element copies)
    // ---------------------------------------------------------------------

    private static class ArrayPinCache
    {
        [ThreadStatic] private static IntPtr s_handle;
        [ThreadStatic] private static GCHandle s_pin;

        internal static GCHandle Pin(Array array, IntPtr arrayHandle)
        {
            if (s_handle == arrayHandle && s_pin.IsAllocated)
            {
                return s_pin;
            }

            ReleaseCurrent();
            s_pin = GCHandle.Alloc(array, GCHandleType.Pinned);
            s_handle = arrayHandle;
            return s_pin;
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
        }
    }

    // ---------------------------------------------------------------------
    // Method invoke cache (metadata + args buffer reuse)
    // ---------------------------------------------------------------------

    private sealed class MethodInvokePlan
    {
        public MethodBase Method;
        public ParameterInfo[] Parameters;
    }

    private static readonly ConditionalWeakTable<MethodBase, MethodInvokePlan> MethodPlans = new();

    [ThreadStatic] private static object[] s_invokeArgs;

    private static MethodInvokePlan GetMethodPlan(IntPtr methodHandle)
    {
        var method = Target<MethodBase>(methodHandle);
        if (method == null)
        {
            return new MethodInvokePlan();
        }

        return MethodPlans.GetValue(method, static m => new MethodInvokePlan
        {
            Method = m,
            Parameters = m.GetParameters(),
        });
    }

    private static object[] RentInvokeArgs(int argc)
    {
        var buffer = s_invokeArgs;
        if (buffer == null || buffer.Length < argc)
        {
            buffer = new object[Math.Max(argc, 4)];
            s_invokeArgs = buffer;
        }

        if (argc > 0)
        {
            Array.Clear(buffer, 0, argc);
        }

        return buffer;
    }

    // ---------------------------------------------------------------------
    // Field access cache (compiled getters/setters for blittable fields)
    // ---------------------------------------------------------------------

    private sealed class FieldAccessPlan
    {
        public FieldInfo Field;
        public Func<object, object> Getter;
        public Action<object, object> Setter;
    }

    private static readonly ConditionalWeakTable<FieldInfo, FieldAccessPlan> FieldPlans = new();

    private static FieldAccessPlan GetFieldPlan(FieldInfo field)
    {
        return FieldPlans.GetValue(field, static f =>
        {
            if (!f.FieldType.IsValueType || !ClrLayout.IsBlittable(f.FieldType))
            {
                return new FieldAccessPlan { Field = f };
            }

            var targetParam = Expression.Parameter(typeof(object), "target");
            var instance = f.IsStatic
                ? null
                : Expression.Convert(
                    Expression.Condition(
                        Expression.Equal(targetParam, Expression.Constant(null)),
                        Expression.Constant(null, f.DeclaringType),
                        Expression.Convert(targetParam, f.DeclaringType)),
                    f.DeclaringType);

            var fieldAccess = Expression.Field(instance, f);
            var boxed = Expression.Convert(fieldAccess, typeof(object));
            var getter = Expression.Lambda<Func<object, object>>(boxed, targetParam).Compile();

            Action<object, object> setter = null;
            if (!f.IsInitOnly)
            {
                var valueParam = Expression.Parameter(typeof(object), "value");
                var unboxed = Expression.Convert(valueParam, f.FieldType);
                var assign = Expression.Assign(Expression.Field(instance, f), unboxed);
                setter = Expression.Lambda<Action<object, object>>(assign, targetParam, valueParam).Compile();
            }

            return new FieldAccessPlan
            {
                Field = f,
                Getter = getter,
                Setter = setter,
            };
        });
    }

    private static unsafe bool TryFieldGetBlittable(FieldInfo field, object target, ref NativeVariant result,
                                             NativeExceptionInfo* exInfo)
    {
        if (result.Kind != NativeVariant.KindBlob)
        {
            return false;
        }

        var plan = GetFieldPlan(field);
        if (plan.Getter == null)
        {
            return false;
        }

        try
        {
            var value = plan.Getter(target);
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
        if (plan.Setter == null)
        {
            return false;
        }

        try
        {
            var managedValue = ClrLayout.Read(field.FieldType, value.Data);
            plan.Setter(target, managedValue);
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
