using System;
using System.Runtime.InteropServices;

namespace Clrpp
{

public static partial class Bridge
{
    /// <summary>
    /// Single native entry point (fetched via hostfxr get_function_pointer).
    /// Fills the given buffer with the addresses of all bridge exports.
    /// The order MUST match the clr_bridge_fn enum in the native clr_bridge.h.
    /// Returns the total number of exports.
    /// </summary>
    [UnmanagedCallersOnly]
    public static unsafe int Bootstrap(IntPtr* slots, int capacity)
    {
        var exports = new IntPtr[]
        {
            // core
            (IntPtr)(delegate* unmanaged<IntPtr, void>)&FreeString,
            (IntPtr)(delegate* unmanaged<IntPtr, void>)&FreeHandle,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&DuplicateHandle,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr, int>)&HandleEquals,
            (IntPtr)(delegate* unmanaged<void>)&GcCollect,
            (IntPtr)(delegate* unmanaged<long>)&GcGetHeapSize,
            (IntPtr)(delegate* unmanaged<long>)&GcGetUsedSize,
            (IntPtr)(delegate* unmanaged<IntPtr, void>)&SetLogCallback,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr, void>)&InternalCalls.Install,

            // runtime
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&DomainCreate,
            (IntPtr)(delegate* unmanaged<IntPtr, int>)&DomainUnload,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&DomainGetName,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr, void>)&DomainAddSearchPath,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr, NativeExceptionInfo*, IntPtr>)&AssemblyLoad,
            (IntPtr)(delegate* unmanaged<IntPtr>)&AssemblyGetCorlib,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&AssemblyGetName,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&AssemblyDumpReferences,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr, IntPtr>)&AssemblyGetType,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr*, int, int>)&AssemblyGetTypes,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr, IntPtr*, int, int>)&AssemblyGetTypesDerivedFrom,

            // type
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&TypeGetName,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&TypeGetNamespace,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&TypeGetFullname,
            (IntPtr)(delegate* unmanaged<IntPtr, int>)&TypeGetFlags,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&TypeGetBaseType,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&TypeGetNestingType,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr*, int, int>)&TypeGetNestedTypes,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr, int>)&TypeIsDerivedFrom,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&TypeGetEnumBaseType,
            (IntPtr)(delegate* unmanaged<IntPtr, long*, IntPtr*, int, int>)&TypeGetEnumValues,
            (IntPtr)(delegate* unmanaged<IntPtr, int>)&TypeGetRank,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&TypeGetElementType,
            (IntPtr)(delegate* unmanaged<IntPtr, int>)&TypeGetSizeof,
            (IntPtr)(delegate* unmanaged<IntPtr, int>)&TypeGetAlignof,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&TypeGetListType,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr, int, IntPtr>)&TypeGetMethod,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr, IntPtr>)&TypeGetMethodBySignature,
            (IntPtr)(delegate* unmanaged<IntPtr, int, IntPtr*, int, int>)&TypeGetMethods,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr, IntPtr>)&TypeGetField,
            (IntPtr)(delegate* unmanaged<IntPtr, int, IntPtr*, int, int>)&TypeGetFields,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr, IntPtr>)&TypeGetProperty,
            (IntPtr)(delegate* unmanaged<IntPtr, int, IntPtr*, int, int>)&TypeGetProperties,
            (IntPtr)(delegate* unmanaged<IntPtr, int, IntPtr*, int, int>)&TypeGetAttributes,

            // method
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&MethodGetName,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&MethodGetFullname,
            (IntPtr)(delegate* unmanaged<IntPtr, int>)&MethodGetFlags,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&MethodGetReturnType,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr*, int, int>)&MethodGetParamTypes,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr*, int, int>)&MethodGetAttributes,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&MethodGetDeclaringType,

            // field
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&FieldGetName,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&FieldGetType,
            (IntPtr)(delegate* unmanaged<IntPtr, int>)&FieldGetFlags,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&FieldGetDeclaringType,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr*, int, int>)&FieldGetAttributes,

            // property
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&PropertyGetName,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&PropertyGetType,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&PropertyGetGetMethod,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&PropertyGetSetMethod,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&PropertyGetDeclaringType,
            (IntPtr)(delegate* unmanaged<IntPtr, int>)&PropertyGetFlags,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr*, int, int>)&PropertyGetAttributes,

            // invoke
            (IntPtr)(delegate* unmanaged<IntPtr, NativeExceptionInfo*, IntPtr>)&ObjectCreate,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&ObjectGetType,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr, int, IntPtr>)&ObjectBox,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr, int, int>)&ObjectUnbox,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&StringCreate,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&StringGetUtf8,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr, NativeVariant*, int, NativeVariant*, NativeExceptionInfo*, void>)&MethodInvoke,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr, NativeVariant*, NativeExceptionInfo*, void>)&FieldGetValue,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr, NativeVariant*, NativeExceptionInfo*, void>)&FieldSetValue,
            (IntPtr)(delegate* unmanaged<IntPtr, long, IntPtr>)&ArrayCreate,
            (IntPtr)(delegate* unmanaged<IntPtr, long>)&ArrayGetLength,
            (IntPtr)(delegate* unmanaged<IntPtr, long, NativeVariant*, NativeExceptionInfo*, void>)&ArrayGetElement,
            (IntPtr)(delegate* unmanaged<IntPtr, long, NativeVariant*, NativeExceptionInfo*, void>)&ArraySetElement,
            (IntPtr)(delegate* unmanaged<IntPtr, long, IntPtr, long, long>)&ArrayCopyTo,
            (IntPtr)(delegate* unmanaged<IntPtr, long, IntPtr, long, long>)&ArrayCopyFrom,

            // appended
            (IntPtr)(delegate* unmanaged<int, void>)&SetInternalCallWeaving,
            (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&InternHandle,
        };

        if (slots != null)
        {
            var count = Math.Min(capacity, exports.Length);
            for (int i = 0; i < count; i++)
            {
                slots[i] = exports[i];
            }
        }

        return exports.Length;
    }
}

} // namespace Clrpp
