using System;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;

namespace Clrpp
{

/// <summary>
/// Desktop-JIT field accessors via DynamicMethod (ldflda/ldsflda + cpblk).
/// Emit into typeof(Bridge).Module (non-collectible) rather than
/// declaring.Module so thunks do not root collectible load contexts.
/// Instance `this` uses CompiledCastFieldTarget&lt;T&gt; (normal managed cast)
/// instead of IL Castclass with a DeclaringType token from another ALC.
/// On failure the caller falls back to Portable.
/// </summary>
public static partial class Bridge
{
    private static bool TryBindCompiledFieldAccess(FieldInfo field, FieldAccessPlan plan)
    {
        try
        {
            if (field.IsStatic)
            {
                plan.CopyOut = CreateStaticFieldCopyOut(field);
                if (!field.IsInitOnly)
                {
                    plan.CopyIn = CreateStaticFieldCopyIn(field);
                }

                return plan.CopyOut != null;
            }

            // Open instance accessors need a reference-type declaring type.
            var declaring = field.DeclaringType;
            if (declaring == null || declaring.IsValueType)
            {
                return false;
            }

            plan.CopyOut = CreateInstanceFieldCopyOut(field);
            if (!field.IsInitOnly)
            {
                plan.CopyIn = CreateInstanceFieldCopyIn(field);
            }

            return plan.CopyOut != null;
        }
        catch (Exception ex)
        {
            Log($"Compiled field accessor unavailable for {field.DeclaringType}.{field.Name}: {ex.Message}",
                "trace");
            plan.CopyOut = null;
            plan.CopyIn = null;
            return false;
        }
    }

    private static FieldBlobCopy CreateInstanceFieldCopyOut(FieldInfo field)
    {
        var declaring = field.DeclaringType
                        ?? throw new InvalidOperationException("Field has no declaring type");
        var dm = new DynamicMethod(
            "clrpp_fld_out_" + field.MetadataToken.ToString("X"),
            typeof(void),
            new[] { typeof(object), typeof(IntPtr), typeof(int) },
            typeof(Bridge).Module,
            skipVisibility: true);
        var il = dm.GetILGenerator();
        il.Emit(OpCodes.Ldarg_1);
        il.Emit(OpCodes.Ldarg_0);
        EmitLoadInstanceFieldAddress(il, declaring, field);
        il.Emit(OpCodes.Ldarg_2);
        il.Emit(OpCodes.Cpblk);
        il.Emit(OpCodes.Ret);
        return dm.CreateDelegate<FieldBlobCopy>();
    }

    private static FieldBlobCopy CreateInstanceFieldCopyIn(FieldInfo field)
    {
        var declaring = field.DeclaringType
                        ?? throw new InvalidOperationException("Field has no declaring type");
        var dm = new DynamicMethod(
            "clrpp_fld_in_" + field.MetadataToken.ToString("X"),
            typeof(void),
            new[] { typeof(object), typeof(IntPtr), typeof(int) },
            typeof(Bridge).Module,
            skipVisibility: true);
        var il = dm.GetILGenerator();
        il.Emit(OpCodes.Ldarg_0);
        EmitLoadInstanceFieldAddress(il, declaring, field);
        il.Emit(OpCodes.Ldarg_1);
        il.Emit(OpCodes.Ldarg_2);
        il.Emit(OpCodes.Cpblk);
        il.Emit(OpCodes.Ret);
        return dm.CreateDelegate<FieldBlobCopy>();
    }

    private static void EmitLoadInstanceFieldAddress(ILGenerator il, Type declaring, FieldInfo field)
    {
        // Normal managed cast in Bridge's module; more reliable across
        // collectible ALCs than IL Castclass with a foreign type token.
        var cast = typeof(Bridge)
            .GetMethod(nameof(CompiledCastFieldTarget),
                       BindingFlags.NonPublic | BindingFlags.Static)
            .MakeGenericMethod(declaring);
        il.Emit(OpCodes.Call, cast);
        il.Emit(OpCodes.Ldflda, field);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static TTarget CompiledCastFieldTarget<TTarget>(object target) where TTarget : class
    {
        return (TTarget)target;
    }

    private static FieldBlobCopy CreateStaticFieldCopyOut(FieldInfo field)
    {
        var dm = new DynamicMethod(
            "clrpp_sfld_out_" + field.MetadataToken.ToString("X"),
            typeof(void),
            new[] { typeof(object), typeof(IntPtr), typeof(int) },
            typeof(Bridge).Module,
            skipVisibility: true);
        var il = dm.GetILGenerator();
        il.Emit(OpCodes.Ldarg_1);
        il.Emit(OpCodes.Ldsflda, field);
        il.Emit(OpCodes.Ldarg_2);
        il.Emit(OpCodes.Cpblk);
        il.Emit(OpCodes.Ret);
        return dm.CreateDelegate<FieldBlobCopy>();
    }

    private static FieldBlobCopy CreateStaticFieldCopyIn(FieldInfo field)
    {
        var dm = new DynamicMethod(
            "clrpp_sfld_in_" + field.MetadataToken.ToString("X"),
            typeof(void),
            new[] { typeof(object), typeof(IntPtr), typeof(int) },
            typeof(Bridge).Module,
            skipVisibility: true);
        var il = dm.GetILGenerator();
        il.Emit(OpCodes.Ldsflda, field);
        il.Emit(OpCodes.Ldarg_1);
        il.Emit(OpCodes.Ldarg_2);
        il.Emit(OpCodes.Cpblk);
        il.Emit(OpCodes.Ret);
        return dm.CreateDelegate<FieldBlobCopy>();
    }
}

} // namespace Clrpp
