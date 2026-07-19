using System;
using System.Reflection;
using System.Runtime.CompilerServices;

namespace Clrpp
{

/// <summary>
/// Field get/set plans. Same feature set on every host:
///   Portable — valuetype OffsetOf+pin, else FieldInfo.GetValue/SetValue
///   Compiled — optional DynamicMethod accelerator when
///              RuntimeFeature.IsDynamicCodeCompiled (desktop JIT)
/// Emit never owns a feature; Portable always fills a complete plan.
/// </summary>
public static partial class Bridge
{
    /// <summary>
    /// True when Reflection.Emit / DynamicMethod produce compiled code.
    /// False under the CoreCLR interpreter — emit is unavailable or only
    /// interpreted (no throughput win).
    /// </summary>
    private static readonly bool CanCompileDynamicCode = RuntimeFeature.IsDynamicCodeCompiled;

    /// <summary>
    /// (target, buffer, size) — target is null for static fields.
    /// Copies field bytes out of (CopyOut) or into (CopyIn) the instance.
    /// </summary>
    private delegate void FieldBlobCopy(object target, IntPtr buffer, int size);

    private sealed class FieldAccessPlan
    {
        public FieldInfo Field;
        public bool IsBlittable;
        public int Size;
        public FieldBlobCopy CopyOut;
        public FieldBlobCopy CopyIn;
    }

    private static readonly ConditionalWeakTable<FieldInfo, FieldAccessPlan> FieldPlans = new();

    private static FieldAccessPlan GetFieldPlan(FieldInfo field)
    {
        return FieldPlans.GetValue(field, static f => CreateFieldPlan(f));
    }

    private static FieldAccessPlan CreateFieldPlan(FieldInfo field)
    {
        var plan = new FieldAccessPlan { Field = field };
        if (!field.FieldType.IsValueType || !ClrLayout.IsBlittable(field.FieldType))
        {
            return plan;
        }

        plan.IsBlittable = true;
        plan.Size = ClrLayout.SizeOf(field.FieldType);

        // Optional accelerator. Any failure falls through to Portable.
        if (CanCompileDynamicCode && TryBindCompiledFieldAccess(field, plan))
        {
            return plan;
        }

        BindPortableFieldAccess(field, plan);
        return plan;
    }

    private static unsafe bool TryFieldGetBlittable(FieldInfo field, object target, ref NativeVariant result,
                                                    NativeExceptionInfo* exInfo)
    {
        if (result.Kind != NativeVariant.KindBlob)
        {
            return false;
        }

        var plan = GetFieldPlan(field);
        if (!plan.IsBlittable || plan.CopyOut == null)
        {
            return false;
        }

        try
        {
            if (plan.Size > result.Size)
            {
                throw new ArgumentException(
                    $"Field {field.Name} value does not fit into {result.Size} bytes");
            }

            plan.CopyOut(target, result.Data, plan.Size);
            result.Size = plan.Size;
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
        if (!plan.IsBlittable || field.IsInitOnly || plan.CopyIn == null)
        {
            return false;
        }

        try
        {
            if (value.Size > 0 && value.Size < plan.Size)
            {
                throw new ArgumentException(
                    $"Blob of {value.Size} bytes is too small for {field.FieldType} ({plan.Size} bytes)");
            }

            plan.CopyIn(target, value.Data, plan.Size);
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
