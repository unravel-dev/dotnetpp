using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Clrpp
{

/// <summary>
/// Public entry point for user assemblies to bind native internal calls.
///
/// The native side registers functions with clr::add_internal_call using the
/// mono style "Full.Type.Name::MethodName(argtypes)" naming.
///
/// Preferred usage: Bind (see InternalCalls.Bind.cs) generates all the
/// marshalling automatically and returns an ordinary delegate:
///
///   static readonly Func&lt;object, string, string&gt; ReturnAString =
///       InternalCalls.Bind&lt;Func&lt;object, string, string&gt;&gt;("Tests.MyObject::ReturnAString");
///
/// Low level usage: fetch the raw pointer once and invoke it through an
/// unmanaged function pointer, marshalling by hand with the helpers below:
///
///   static readonly unsafe delegate* unmanaged[Cdecl]&lt;IntPtr, float, void&gt;
///       DoStuffFn = (delegate* unmanaged[Cdecl]&lt;IntPtr, float, void&gt;)
///           InternalCalls.Get("Tests.MyObject::DoStuff(single)");
/// </summary>
public static unsafe partial class InternalCalls
{
    // Installed from native during bootstrap:
    //   resolver(nameUtf8) -> function pointer (or null)
    private static delegate* unmanaged[Cdecl]<IntPtr, IntPtr> resolver;

    // pendingException() -> utf8 "Namespace|ClassName|Message" (native owned,
    // freed by native after the call returns) or null when nothing is pending.
    private static delegate* unmanaged[Cdecl]<IntPtr> pendingExceptionQuery;

    [UnmanagedCallersOnly]
    internal static void Install(IntPtr resolverFn, IntPtr pendingExceptionFn)
    {
        resolver = (delegate* unmanaged[Cdecl]<IntPtr, IntPtr>)resolverFn;
        pendingExceptionQuery = (delegate* unmanaged[Cdecl]<IntPtr>)pendingExceptionFn;
    }

    /// <summary>Resolve a native internal call by its registered name.</summary>
    public static IntPtr Get(string name)
    {
        if (resolver == null)
        {
            throw new InvalidOperationException("Clrpp native runtime is not initialized");
        }

        var nameUtf8 = Marshal.StringToCoTaskMemUTF8(name);
        try
        {
            var fn = resolver(nameUtf8);
            if (fn == IntPtr.Zero)
            {
                throw new MissingMethodException($"Internal call not registered: {name}");
            }

            return fn;
        }
        finally
        {
            Marshal.FreeCoTaskMem(nameUtf8);
        }
    }

    /// <summary>Try-resolve variant that returns IntPtr.Zero when missing.</summary>
    public static IntPtr TryGet(string name)
    {
        if (resolver == null)
        {
            return IntPtr.Zero;
        }

        var nameUtf8 = Marshal.StringToCoTaskMemUTF8(name);
        try
        {
            return resolver(nameUtf8);
        }
        finally
        {
            Marshal.FreeCoTaskMem(nameUtf8);
        }
    }

    /// <summary>
    /// Resolve following mono's icall lookup order: the full signature name
    /// ("Ns.Type::Method(single,string)") first, then the bare
    /// "Ns.Type::Method". Used by IL woven from mono-style
    /// [MethodImpl(MethodImplOptions.InternalCall)] extern methods (see
    /// Weaver.cs). Throws MissingMethodException when neither is registered.
    /// </summary>
    public static IntPtr GetPtr(string primaryName, string fallbackName)
    {
        var fn = TryGetPtr(primaryName, fallbackName);
        if (fn == IntPtr.Zero)
        {
            throw new MissingMethodException($"Internal call not registered: {primaryName}");
        }

        return fn;
    }

    /// <summary>Non-throwing variant of GetPtr (used by load-time pre-binding).</summary>
    public static IntPtr TryGetPtr(string primaryName, string fallbackName)
    {
        var fn = TryGet(primaryName);
        return fn != IntPtr.Zero ? fn : TryGet(fallbackName);
    }

    /// <summary>
    /// Throws the exception raised by the native side via clr::raise_exception
    /// during the last internal call on this thread, if any. CoreCLR cannot
    /// throw managed exceptions across the unmanaged boundary, so wrappers
    /// generated for internal calls should call this right after invoking the
    /// native function pointer.
    /// </summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void ThrowIfPending()
    {
        if (pendingExceptionQuery == null)
        {
            return;
        }

        var pending = pendingExceptionQuery();
        if (pending == IntPtr.Zero)
        {
            return;
        }

        var payload = Marshal.PtrToStringUTF8(pending) ?? string.Empty;
        var parts = payload.Split('|', 3);
        var ns = parts.Length > 0 ? parts[0] : string.Empty;
        var cls = parts.Length > 1 ? parts[1] : "Exception";
        var message = parts.Length > 2 ? parts[2] : string.Empty;

        throw CreateException(ns, cls, message);
    }

    // -- string marshalling helpers for internal call arguments/returns ------

    /// <summary>Allocate a utf8 copy to pass as a string argument. Free with FreeUtf8.</summary>
    public static IntPtr AllocUtf8(string value)
    {
        return Marshal.StringToCoTaskMemUTF8(value);
    }

    /// <summary>Free a string allocated with AllocUtf8.</summary>
    public static void FreeUtf8(IntPtr ptr)
    {
        Marshal.FreeCoTaskMem(ptr);
    }

    /// <summary>
    /// Read and free a utf8 string returned by a native internal call
    /// (native returns interop allocations the managed side owns).
    /// </summary>
    public static string ConsumeUtf8(IntPtr ptr)
    {
        if (ptr == IntPtr.Zero)
        {
            return null;
        }

        var result = Marshal.PtrToStringUTF8(ptr);
        Marshal.FreeCoTaskMem(ptr);
        return result;
    }

    // -- object handle helpers for internal call arguments/returns -----------

    /// <summary>Allocate a GCHandle to pass an object argument. Free with FreeHandle.</summary>
    public static IntPtr AllocHandle(object obj)
    {
        return obj == null ? IntPtr.Zero : GCHandle.ToIntPtr(GCHandle.Alloc(obj));
    }

    /// <summary>Free a handle allocated with AllocHandle.</summary>
    public static void FreeHandle(IntPtr handle)
    {
        if (handle != IntPtr.Zero)
        {
            GCHandle.FromIntPtr(handle).Free();
        }
    }

    /// <summary>
    /// Take the target of a handle returned by a native internal call and
    /// free it (native returns handles whose ownership transfers to the
    /// managed side).
    /// </summary>
    public static object ConsumeHandle(IntPtr handle)
    {
        if (handle == IntPtr.Zero)
        {
            return null;
        }

        var gc = GCHandle.FromIntPtr(handle);
        var target = gc.Target;
        // Interned handles (reflection objects) live for the whole session.
        if (!Bridge.IsInterned(handle))
        {
            gc.Free();
        }
        return target;
    }

    private static Exception CreateException(string ns, string cls, string message)
    {
        var fullName = string.IsNullOrEmpty(ns) ? cls : ns + "." + cls;

        var type = Type.GetType(fullName, throwOnError: false);
        if (type != null && typeof(Exception).IsAssignableFrom(type))
        {
            try
            {
                return (Exception)Activator.CreateInstance(type, message);
            }
            catch
            {
                // fall through to generic exception
            }
        }

        return new Exception($"{fullName}: {message}");
    }
}

} // namespace Clrpp
