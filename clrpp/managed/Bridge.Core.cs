using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

namespace Clrpp
{

/// <summary>
/// Blittable variant used to pass values between native and managed code.
/// Layout must match the native clr_variant struct in clr_type_conversion.h.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct NativeVariant
{
    public const int KindEmpty = 0;        // no value / null
    public const int KindBlob = 1;         // Data points to raw value bytes, Size = byte count
    public const int KindStringUtf8 = 2;   // Data points to null terminated utf8
    public const int KindObjectHandle = 3; // Data is a GCHandle (IntPtr)

    public int Kind;
    public int Size;
    public IntPtr Data;
}

/// <summary>
/// Exception info marshalled to native code. All strings are CoTaskMem utf8
/// allocations owned by the native side (freed via FreeString).
/// Layout must match native clr_exception_info.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct NativeExceptionInfo
{
    public IntPtr TypeName;
    public IntPtr Message;
    public IntPtr Source;
    public IntPtr StackTrace;
    public int HasValue;
}

public static partial class Bridge
{
    static Bridge()
    {
        // The bridge is loaded without a deps.json, so its load context has
        // no probing logic. Resolve dependencies (Mono.Cecil for the icall
        // weaver) from the bridge's own directory.
        var self = typeof(Bridge).Assembly;
        var context = AssemblyLoadContext.GetLoadContext(self);
        var directory = System.IO.Path.GetDirectoryName(self.Location);
        if (context != null && !string.IsNullOrEmpty(directory))
        {
            context.Resolving += (loadContext, name) =>
            {
                var candidate = System.IO.Path.Combine(directory, name.Name + ".dll");
                return System.IO.File.Exists(candidate) ? loadContext.LoadFromAssemblyPath(candidate) : null;
            };
        }
    }

    // ---------------------------------------------------------------------
    // Handle management
    //
    // Reflection objects (Type / MethodInfo / FieldInfo / PropertyInfo /
    // Assembly / AssemblyLoadContext) get interned handles: the same object
    // always maps to the same IntPtr and the handle lives until Shutdown.
    // Object instances get regular strong handles which native code owns and
    // frees explicitly.
    // ---------------------------------------------------------------------

    private static readonly ConcurrentDictionary<object, IntPtr> InternedHandles = new(ReferenceEqualityComparer.Instance);
    private static readonly ConcurrentDictionary<IntPtr, GCHandle> InternedHandlesReverse = new();

    internal static IntPtr Intern(object obj)
    {
        if (obj == null)
        {
            return IntPtr.Zero;
        }

        return InternedHandles.GetOrAdd(obj, static o =>
        {
            var handle = GCHandle.Alloc(o);
            var ptr = GCHandle.ToIntPtr(handle);
            InternedHandlesReverse[ptr] = handle;
            return ptr;
        });
    }

    internal static IntPtr NewObjectHandle(object obj)
    {
        if (obj == null)
        {
            return IntPtr.Zero;
        }

        return GCHandle.ToIntPtr(GCHandle.Alloc(obj));
    }

    internal static object Target(IntPtr handle)
    {
        if (handle == IntPtr.Zero)
        {
            return null;
        }

        // A stale/freed handle is a native-side bug, but it must degrade to
        // a diagnosable "invalid handle" error instead of an uncatchable
        // InvalidOperationException escaping an UnmanagedCallersOnly export
        // (which would fail-fast the whole process).
        try
        {
            return GCHandle.FromIntPtr(handle).Target;
        }
        catch (InvalidOperationException)
        {
            Log($"invalid/freed GCHandle passed from native code: 0x{handle:X}", "error");
            return null;
        }
    }

    internal static T Target<T>(IntPtr handle) where T : class
    {
        return Target(handle) as T;
    }

    internal static IntPtr AllocUtf8(string str)
    {
        if (str == null)
        {
            return IntPtr.Zero;
        }

        return Marshal.StringToCoTaskMemUTF8(str);
    }

    internal static string ReadUtf8(IntPtr ptr)
    {
        if (ptr == IntPtr.Zero)
        {
            return null;
        }

        return Marshal.PtrToStringUTF8(ptr);
    }

    internal static void FillException(Exception ex, ref NativeExceptionInfo info)
    {
        if (ex is TargetInvocationException tie && tie.InnerException != null)
        {
            ex = tie.InnerException;
        }

        info.TypeName = AllocUtf8(ex.GetType().FullName);
        info.Message = AllocUtf8(ex.Message ?? string.Empty);
        info.Source = AllocUtf8(ex.Source ?? string.Empty);
        info.StackTrace = AllocUtf8(ex.StackTrace ?? string.Empty);
        info.HasValue = 1;
    }

    // ---------------------------------------------------------------------
    // Value conversion between NativeVariant and managed objects
    // ---------------------------------------------------------------------

    internal static object VariantToObject(in NativeVariant variant, Type expectedType)
    {
        switch (variant.Kind)
        {
            case NativeVariant.KindEmpty:
                return null;

            case NativeVariant.KindStringUtf8:
                return ReadUtf8(variant.Data);

            case NativeVariant.KindObjectHandle:
                return Target(variant.Data);

            case NativeVariant.KindBlob:
            {
                if (expectedType == null)
                {
                    throw new ArgumentException("Blob variant requires an expected type");
                }

                // The blob is a raw C++ value copy - read it with the CLR
                // layout (see ClrLayout; covers primitives, enums and
                // blittable structs). Marshal.PtrToStructure would misread
                // bool/char fields.
                var expected = ClrLayout.SizeOf(expectedType);
                if (variant.Size > 0 && variant.Size < expected)
                {
                    throw new ArgumentException(
                        $"Blob of {variant.Size} bytes is too small for {expectedType} ({expected} bytes)");
                }

                return ClrLayout.Read(expectedType, variant.Data);
            }

            default:
                throw new ArgumentException($"Unknown variant kind {variant.Kind}");
        }
    }

    /// Boxes the result as a new object handle variant (native side owns the handle).
    internal static NativeVariant ObjectToVariant(object obj)
    {
        var variant = default(NativeVariant);
        if (obj == null)
        {
            variant.Kind = NativeVariant.KindEmpty;
            return variant;
        }

        variant.Kind = NativeVariant.KindObjectHandle;
        variant.Data = NewObjectHandle(obj);
        return variant;
    }

    // ---------------------------------------------------------------------
    // Generic native exports
    // ---------------------------------------------------------------------

    [UnmanagedCallersOnly]
    public static void FreeString(IntPtr ptr)
    {
        if (ptr != IntPtr.Zero)
        {
            Marshal.FreeCoTaskMem(ptr);
        }
    }

    [UnmanagedCallersOnly]
    public static void FreeHandle(IntPtr handle)
    {
        if (handle == IntPtr.Zero)
        {
            return;
        }

        // Interned handles live for the whole runtime session.
        if (InternedHandlesReverse.ContainsKey(handle))
        {
            return;
        }

        try
        {
            GCHandle.FromIntPtr(handle).Free();
        }
        catch (InvalidOperationException)
        {
            Log($"FreeHandle: invalid/already-freed GCHandle: 0x{handle:X}", "error");
        }
    }

    [UnmanagedCallersOnly]
    public static IntPtr DuplicateHandle(IntPtr handle)
    {
        if (handle == IntPtr.Zero)
        {
            return IntPtr.Zero;
        }

        // Interned handles are stable, hand out the same one.
        if (InternedHandlesReverse.ContainsKey(handle))
        {
            return handle;
        }

        var target = Target(handle);
        return target != null ? GCHandle.ToIntPtr(GCHandle.Alloc(target)) : IntPtr.Zero;
    }

    /// <summary>
    /// Canonicalize a handle to the interned handle for its target. Used for
    /// reflection objects (System.Type etc.) that arrive through plain
    /// GCHandles - e.g. internal call arguments - so native wrappers can hold
    /// a session-lifetime reference.
    /// </summary>
    [UnmanagedCallersOnly]
    public static IntPtr InternHandle(IntPtr handle)
    {
        if (handle == IntPtr.Zero)
        {
            return IntPtr.Zero;
        }

        if (InternedHandlesReverse.ContainsKey(handle))
        {
            return handle;
        }

        return Intern(Target(handle));
    }

    internal static bool IsInterned(IntPtr handle)
    {
        return InternedHandlesReverse.ContainsKey(handle);
    }

    [UnmanagedCallersOnly]
    public static int HandleEquals(IntPtr a, IntPtr b)
    {
        if (a == b)
        {
            return 1;
        }

        var ta = Target(a);
        var tb = Target(b);
        return ReferenceEquals(ta, tb) ? 1 : 0;
    }

    // ---------------------------------------------------------------------
    // GC
    // ---------------------------------------------------------------------

    [UnmanagedCallersOnly]
    public static void GcCollect()
    {
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
    }

    [UnmanagedCallersOnly]
    public static long GcGetHeapSize()
    {
        var info = GC.GetGCMemoryInfo();
        return info.HeapSizeBytes;
    }

    [UnmanagedCallersOnly]
    public static long GcGetUsedSize()
    {
        return GC.GetTotalMemory(false);
    }

    // ---------------------------------------------------------------------
    // Diagnostics
    // ---------------------------------------------------------------------

    [UnmanagedCallersOnly]
    public static int IsDebuggerAttached()
    {
        return System.Diagnostics.Debugger.IsAttached ? 1 : 0;
    }

    // ---------------------------------------------------------------------
    // Logging callback (native side installs a function pointer)
    // ---------------------------------------------------------------------

    internal static unsafe delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> NativeLogCallback;

    [UnmanagedCallersOnly]
    public static unsafe void SetLogCallback(IntPtr callback)
    {
        NativeLogCallback = (delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void>)callback;
    }

    internal static unsafe void Log(string message, string category)
    {
        if (NativeLogCallback == null)
        {
            return;
        }

        var msg = AllocUtf8(message);
        var cat = AllocUtf8(category);
        try
        {
            NativeLogCallback(msg, cat);
        }
        finally
        {
            Marshal.FreeCoTaskMem(msg);
            Marshal.FreeCoTaskMem(cat);
        }
    }
}

} // namespace Clrpp
