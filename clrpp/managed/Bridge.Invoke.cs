using System;
using System.Reflection;
using System.Runtime.InteropServices;

namespace Clrpp
{

public static partial class Bridge
{
    // ---------------------------------------------------------------------
    // Objects
    // ---------------------------------------------------------------------

    [UnmanagedCallersOnly]
    public static unsafe IntPtr ObjectCreate(IntPtr typeHandle, NativeExceptionInfo* exInfo)
    {
        try
        {
            var type = Target<Type>(typeHandle);
            if (type == null)
            {
                throw new ArgumentException("Invalid type handle");
            }

            var obj = Activator.CreateInstance(type, nonPublic: true);
            return NewObjectHandle(obj);
        }
        catch (Exception ex)
        {
            FillException(ex, ref *exInfo);
            return IntPtr.Zero;
        }
    }

    [UnmanagedCallersOnly]
    public static IntPtr ObjectGetType(IntPtr objectHandle)
    {
        var obj = Target(objectHandle);
        return obj != null ? Intern(obj.GetType()) : IntPtr.Zero;
    }

    /// Box raw value bytes into a new object handle.
    [UnmanagedCallersOnly]
    public static IntPtr ObjectBox(IntPtr typeHandle, IntPtr data, int size)
    {
        var type = Target<Type>(typeHandle);
        if (type == null || data == IntPtr.Zero)
        {
            return IntPtr.Zero;
        }

        // Conversion throws for non-value/reference-bearing types; that must
        // not escape an UnmanagedCallersOnly export (process fail-fast).
        try
        {
            var variant = new NativeVariant { Kind = NativeVariant.KindBlob, Data = data, Size = size };
            var obj = VariantToObject(variant, type);
            return NewObjectHandle(obj);
        }
        catch (Exception ex)
        {
            Log($"ObjectBox failed for {type}: {ex.Message}", "error");
            return IntPtr.Zero;
        }
    }

    /// Unbox an object's value into a native buffer. Returns bytes written or -1.
    [UnmanagedCallersOnly]
    public static int ObjectUnbox(IntPtr objectHandle, IntPtr buffer, int size)
    {
        var obj = Target(objectHandle);
        if (obj == null || buffer == IntPtr.Zero)
        {
            return -1;
        }

        try
        {
            return WriteValue(obj, buffer, size);
        }
        catch (Exception ex)
        {
            Log($"ObjectUnbox failed for {obj.GetType()}: {ex.Message}", "error");
            return -1;
        }
    }

    /// Writes a value's raw bytes into a native buffer using the CLR layout
    /// (see ClrLayout - the native side reads them as a C++ struct copy).
    /// Handles primitives, enums and blittable structs; returns bytes
    /// written, or -1 when the value does not fit.
    internal static int WriteValue(object value, IntPtr buffer, int size)
    {
        return ClrLayout.Write(value, buffer, size);
    }

    // ---------------------------------------------------------------------
    // Strings
    // ---------------------------------------------------------------------

    [UnmanagedCallersOnly]
    public static IntPtr StringCreate(IntPtr utf8)
    {
        var str = ReadUtf8(utf8) ?? string.Empty;
        return NewObjectHandle(str);
    }

    [UnmanagedCallersOnly]
    public static IntPtr StringGetUtf8(IntPtr objectHandle)
    {
        var str = Target(objectHandle) as string;
        return AllocUtf8(str ?? string.Empty);
    }

    // ---------------------------------------------------------------------
    // Method invocation
    // ---------------------------------------------------------------------

    /// <summary>
    /// Invoke a method. Args are converted using the declared parameter types.
    /// The result variant's Kind field on input requests a representation:
    ///   KindEmpty        - discard result / auto (object handle if not null)
    ///   KindBlob         - write raw value bytes into result.Data (result.Size = capacity)
    ///   KindStringUtf8   - result.Data receives a utf8 allocation
    ///   KindObjectHandle - result.Data receives a new strong handle
    /// </summary>
    [UnmanagedCallersOnly]
    public static unsafe void MethodInvoke(IntPtr methodHandle, IntPtr targetHandle,
                                           NativeVariant* args, int argc,
                                           NativeVariant* result, NativeExceptionInfo* exInfo)
    {
        object[] managedArgs = null;
        try
        {
            var plan = GetMethodPlan(methodHandle);
            if (plan.Method == null)
            {
                throw new ArgumentException("Invalid method handle");
            }

            // Blittable CreateDelegate path: no object[] / no boxing.
            if (plan.BlittableInvoke != null &&
                plan.BlittableArgc == argc &&
                (argc == 0 || ArgsAreBlobs(args, argc)) &&
                (result->Kind == NativeVariant.KindBlob || result->Kind == NativeVariant.KindEmpty))
            {
                var target = Target(targetHandle);
                plan.BlittableInvoke(target, args, result);
                return;
            }

            var targetObj = Target(targetHandle);
            var parameters = plan.Parameters ?? plan.Method.GetParameters();

            if (argc > 0)
            {
                managedArgs = RentInvokeArgs(argc);
                for (int i = 0; i < argc; i++)
                {
                    var expected = i < parameters.Length ? parameters[i].ParameterType : null;
                    managedArgs[i] = VariantToObject(in args[i], expected);
                }
            }

            var returnValue = InvokeWithPlan(plan, targetObj, managedArgs);
            WriteResult(returnValue, ref *result);
        }
        catch (Exception ex)
        {
            FillException(ex, ref *exInfo);
        }
        finally
        {
            ReturnInvokeArgs(managedArgs, argc);
        }
    }

    /// Reflection objects are canonical singletons; hand out interned handles
    /// so native caches stay coherent. Everything else gets a fresh handle.
    internal static IntPtr HandleFor(object obj)
    {
        if (obj is Type || obj is System.Reflection.MethodBase || obj is System.Reflection.FieldInfo ||
            obj is System.Reflection.PropertyInfo || obj is System.Reflection.Assembly)
        {
            return Intern(obj);
        }

        return NewObjectHandle(obj);
    }

    internal static void WriteResult(object returnValue, ref NativeVariant result)
    {
        switch (result.Kind)
        {
            case NativeVariant.KindEmpty:
                if (returnValue != null)
                {
                    result.Kind = NativeVariant.KindObjectHandle;
                    result.Data = HandleFor(returnValue);
                }
                break;

            case NativeVariant.KindBlob:
                if (returnValue == null)
                {
                    result.Kind = NativeVariant.KindEmpty;
                    result.Size = 0;
                }
                else
                {
                    var written = WriteValue(returnValue, result.Data, result.Size);
                    if (written < 0)
                    {
                        throw new ArgumentException(
                            $"Return value of type {returnValue.GetType()} does not fit into {result.Size} bytes");
                    }
                    result.Size = written;
                }
                break;

            case NativeVariant.KindStringUtf8:
                if (returnValue == null)
                {
                    result.Kind = NativeVariant.KindEmpty;
                    result.Data = IntPtr.Zero;
                }
                else
                {
                    result.Data = AllocUtf8(returnValue.ToString());
                }
                break;

            case NativeVariant.KindObjectHandle:
                if (returnValue == null)
                {
                    result.Kind = NativeVariant.KindEmpty;
                    result.Data = IntPtr.Zero;
                }
                else
                {
                    result.Data = HandleFor(returnValue);
                }
                break;
        }
    }

    // ---------------------------------------------------------------------
    // Field access
    // ---------------------------------------------------------------------

    [UnmanagedCallersOnly]
    public static unsafe void FieldGetValue(IntPtr fieldHandle, IntPtr targetHandle,
                                            NativeVariant* result, NativeExceptionInfo* exInfo)
    {
        try
        {
            var field = Target<FieldInfo>(fieldHandle);
            if (field == null)
            {
                throw new ArgumentException("Invalid field handle");
            }

            var target = Target(targetHandle);
            if (TryFieldGetBlittable(field, target, ref *result, exInfo))
            {
                return;
            }

            var value = field.GetValue(target);
            WriteResult(value, ref *result);
        }
        catch (Exception ex)
        {
            FillException(ex, ref *exInfo);
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe void FieldSetValue(IntPtr fieldHandle, IntPtr targetHandle,
                                            NativeVariant* value, NativeExceptionInfo* exInfo)
    {
        try
        {
            var field = Target<FieldInfo>(fieldHandle);
            if (field == null)
            {
                throw new ArgumentException("Invalid field handle");
            }

            var target = Target(targetHandle);
            if (value->Kind == NativeVariant.KindBlob &&
                TryFieldSetBlittable(field, target, in *value, exInfo))
            {
                return;
            }

            var managedValue = VariantToObject(in *value, field.FieldType);

            if (managedValue != null && !field.FieldType.IsValueType &&
                !field.FieldType.IsInstanceOfType(managedValue))
            {
                throw new ArgumentException(
                    $"Value of type {managedValue.GetType()} is not assignable to field of type {field.FieldType}");
            }

            field.SetValue(target, managedValue);
        }
        catch (Exception ex)
        {
            FillException(ex, ref *exInfo);
        }
    }

    // ---------------------------------------------------------------------
    // Arrays
    // ---------------------------------------------------------------------

    [UnmanagedCallersOnly]
    public static IntPtr ArrayCreate(IntPtr elementTypeHandle, long length)
    {
        var elementType = Target<Type>(elementTypeHandle);
        if (elementType == null)
        {
            return IntPtr.Zero;
        }

        try
        {
            var array = Array.CreateInstance(elementType, length);
            return NewObjectHandle(array);
        }
        catch (Exception ex)
        {
            Log($"ArrayCreate failed for {elementType}[{length}]: {ex.Message}", "error");
            return IntPtr.Zero;
        }
    }

    [UnmanagedCallersOnly]
    public static long ArrayGetLength(IntPtr arrayHandle)
    {
        var array = Target(arrayHandle) as Array;
        return array?.LongLength ?? 0;
    }

    [UnmanagedCallersOnly]
    public static unsafe void ArrayGetElement(IntPtr arrayHandle, long index,
                                              NativeVariant* result, NativeExceptionInfo* exInfo)
    {
        try
        {
            if (Target(arrayHandle) is not Array array)
            {
                throw new ArgumentException("Invalid array handle");
            }

            WriteResult(array.GetValue(index), ref *result);
        }
        catch (Exception ex)
        {
            FillException(ex, ref *exInfo);
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe void ArraySetElement(IntPtr arrayHandle, long index,
                                              NativeVariant* value, NativeExceptionInfo* exInfo)
    {
        try
        {
            if (Target(arrayHandle) is not Array array)
            {
                throw new ArgumentException("Invalid array handle");
            }

            var elementType = array.GetType().GetElementType();
            array.SetValue(VariantToObject(in *value, elementType), index);
        }
        catch (Exception ex)
        {
            FillException(ex, ref *exInfo);
        }
    }

    /// Bulk copy out of a blittable-element array. Returns bytes copied or -1.
    [UnmanagedCallersOnly]
    public static unsafe long ArrayCopyTo(IntPtr arrayHandle, long byteOffset, IntPtr dest, long byteCount)
    {
        if (Target(arrayHandle) is not Array array || dest == IntPtr.Zero || byteOffset < 0 || byteCount < 0)
        {
            return -1;
        }

        try
        {
            var pin = ArrayPinCache.Pin(array, arrayHandle);
            var total = ArrayPinCache.ByteLength(array);
            if (byteOffset > total)
            {
                return -1;
            }

            var count = Math.Min(byteCount, total - byteOffset);
            if (count == 0)
            {
                return 0;
            }

            Buffer.MemoryCopy((byte*)pin.AddrOfPinnedObject() + byteOffset, (void*)dest, byteCount, count);
            return count;
        }
        catch (Exception ex)
        {
            Log($"ArrayCopyTo failed for {array.GetType()}: {ex.Message}", "error");
            return -1;
        }
    }

    /// Bulk copy into a blittable-element array. Returns bytes copied or -1.
    [UnmanagedCallersOnly]
    public static unsafe long ArrayCopyFrom(IntPtr arrayHandle, long byteOffset, IntPtr src, long byteCount)
    {
        if (Target(arrayHandle) is not Array array || src == IntPtr.Zero || byteOffset < 0 || byteCount < 0)
        {
            return -1;
        }

        try
        {
            var pin = ArrayPinCache.Pin(array, arrayHandle);
            var total = ArrayPinCache.ByteLength(array);
            if (byteOffset > total)
            {
                return -1;
            }

            var count = Math.Min(byteCount, total - byteOffset);
            if (count == 0)
            {
                return 0;
            }

            Buffer.MemoryCopy((void*)src, (byte*)pin.AddrOfPinnedObject() + byteOffset, total - byteOffset, count);
            return count;
        }
        catch (Exception ex)
        {
            Log($"ArrayCopyFrom failed for {array.GetType()}: {ex.Message}", "error");
            return -1;
        }
    }
}

} // namespace Clrpp
