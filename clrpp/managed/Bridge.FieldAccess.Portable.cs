using System;
using System.Reflection;
using System.Runtime.InteropServices;

namespace Clrpp
{

/// <summary>
/// Interpreter-safe field accessors. Always available; source of truth for
/// feature parity. Matches the pre-split FastPath semantics:
///   - valuetype declaring type: OffsetOf + short-lived pin + memcpy
///   - otherwise: FieldInfo.GetValue/SetValue + ClrLayout
/// Class layouts stay on GetValue/SetValue (Auto layout OffsetOf is unsafe).
/// </summary>
public static partial class Bridge
{
    private static void BindPortableFieldAccess(FieldInfo field, FieldAccessPlan plan)
    {
        if (field.IsStatic)
        {
            plan.CopyOut = (target, buffer, size) =>
            {
                var value = field.GetValue(null);
                if (ClrLayout.Write(value, buffer, size) < 0)
                {
                    throw new ArgumentException(
                        $"Field {field.Name} value does not fit into {size} bytes");
                }
            };
            if (!field.IsInitOnly)
            {
                plan.CopyIn = (target, buffer, size) =>
                {
                    var managedValue = ClrLayout.Read(field.FieldType, buffer);
                    field.SetValue(null, managedValue);
                };
            }

            return;
        }

        if (TryGetInstanceFieldOffset(field, out var offset))
        {
            var declaring = field.DeclaringType;
            var fieldSize = plan.Size;
            plan.CopyOut = (target, buffer, size) =>
            {
                if (target == null || !declaring.IsInstanceOfType(target))
                {
                    throw new ArgumentException(
                        $"Target is not an instance of {declaring} for field {field.Name}");
                }

                if (fieldSize > size)
                {
                    throw new ArgumentException(
                        $"Field {field.Name} value does not fit into {size} bytes");
                }

                var pin = GCHandle.Alloc(target, GCHandleType.Pinned);
                try
                {
                    unsafe
                    {
                        Buffer.MemoryCopy(
                            (byte*)pin.AddrOfPinnedObject() + offset,
                            (void*)buffer,
                            size,
                            fieldSize);
                    }
                }
                finally
                {
                    pin.Free();
                }
            };
            if (!field.IsInitOnly)
            {
                plan.CopyIn = (target, buffer, size) =>
                {
                    if (target == null || !declaring.IsInstanceOfType(target))
                    {
                        throw new ArgumentException(
                            $"Target is not an instance of {declaring} for field {field.Name}");
                    }

                    var pin = GCHandle.Alloc(target, GCHandleType.Pinned);
                    try
                    {
                        unsafe
                        {
                            Buffer.MemoryCopy(
                                (void*)buffer,
                                (byte*)pin.AddrOfPinnedObject() + offset,
                                fieldSize,
                                fieldSize);
                        }
                    }
                    finally
                    {
                        pin.Free();
                    }
                };
            }

            return;
        }

        plan.CopyOut = (target, buffer, size) =>
        {
            var value = field.GetValue(target);
            if (ClrLayout.Write(value, buffer, size) < 0)
            {
                throw new ArgumentException(
                    $"Field {field.Name} value does not fit into {size} bytes");
            }
        };
        if (!field.IsInitOnly)
        {
            plan.CopyIn = (target, buffer, size) =>
            {
                var managedValue = ClrLayout.Read(field.FieldType, buffer);
                field.SetValue(target, managedValue);
            };
        }
    }

    private static bool TryGetInstanceFieldOffset(FieldInfo field, out int offset)
    {
        offset = 0;
        var declaring = field.DeclaringType;
        // Same restriction as the pre-split FastPath: OffsetOf is only used
        // for valuetypes. Class Auto layout must not go through pin+offset.
        //
        // The declaring value type itself must be blittable: this path pins the
        // boxed target, and GCHandle.Alloc(Pinned) throws for a struct that
        // carries references (or auto layout) even when the field being read is
        // blittable. Fall through to FieldInfo.GetValue/SetValue in that case.
        if (declaring == null || !declaring.IsValueType || !ClrLayout.IsBlittable(declaring))
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
}

} // namespace Clrpp
