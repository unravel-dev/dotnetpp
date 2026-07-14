using System;
using System.Runtime.CompilerServices;

namespace Clrpp
{

/// <summary>
/// Raw value copies using the CLR's own field layout.
///
/// The native side memcpys C++ structs, and for blittable data the CLR
/// layout matches C++ byte for byte (bool = 1 byte, char = 2-byte utf16,
/// natural padding). Marshal.SizeOf / StructureToPtr / PtrToStructure must
/// NOT be used on this path: they apply the interop-marshalled layout,
/// where bool becomes a 4-byte BOOL and char an ANSI byte, silently
/// corrupting any struct containing them.
///
/// Types whose layout the GC tracks (reference fields) are rejected - a raw
/// copy of those would hand untracked object pointers to native code.
///
/// AOT note: For() instantiates Ops&lt;T&gt; over arbitrary value types via
/// MakeGenericType. That is reflection, not code generation - Mono full-AOT
/// (the realistic iOS runtime) handles unseen value-type instantiations
/// through its gsharedvt fallback. NativeAOT does not: instantiations the
/// compiler never saw are missing from the image, so targeting NativeAOT
/// would require a non-generic rewrite (pinned-box copies) or rd.xml roots
/// for every struct crossing this path.
/// </summary>
internal static class ClrLayout
{
    /// Size of a value type as laid out by the runtime (not Marshal.SizeOf).
    public static int SizeOf(Type type) => For(type).Size;

    /// Reads a boxed value from a native buffer.
    public static object Read(Type type, IntPtr buffer) => For(type).Read(buffer);

    /// Writes a boxed value into a native buffer of `capacity` bytes.
    /// Returns bytes written, or -1 when the value does not fit.
    public static int Write(object value, IntPtr buffer, int capacity)
    {
        var ops = For(value.GetType());
        if (ops.Size > capacity)
        {
            return -1;
        }

        ops.Write(value, buffer);
        return ops.Size;
    }

    /// True when values of this type can cross the native boundary by raw
    /// copy: a value type without object references.
    public static bool IsBlittable(Type type)
    {
        if (!type.IsValueType)
        {
            return false;
        }

        try
        {
            For(type);
            return true;
        }
        catch
        {
            return false;
        }
    }

    // -----------------------------------------------------------------------

    private abstract class Ops
    {
        public abstract int Size { get; }
        public abstract object Read(IntPtr buffer);
        public abstract void Write(object value, IntPtr buffer);
    }

    private sealed class Ops<T> : Ops where T : struct
    {
        public Ops()
        {
            if (RuntimeHelpers.IsReferenceOrContainsReferences<T>())
            {
                throw new ArgumentException(
                    $"{typeof(T)} contains object references and cannot cross the native boundary by value");
            }
        }

        public override int Size => Unsafe.SizeOf<T>();

        // Unaligned: native buffers (variant blobs, array slices) carry no
        // alignment guarantee.
        public override unsafe object Read(IntPtr buffer) => Unsafe.ReadUnaligned<T>((void*)buffer);

        public override unsafe void Write(object value, IntPtr buffer) =>
            Unsafe.WriteUnaligned((void*)buffer, (T)value);
    }

    // Weak keys: a strong Type -> Ops map would root collectible assemblies
    // (unloaded script domains) forever. Entries die with their type.
    private static readonly ConditionalWeakTable<Type, Ops> Cache = new();

    private static Ops For(Type type)
    {
        return Cache.GetValue(type, static t =>
        {
            if (!t.IsValueType)
            {
                throw new ArgumentException($"{t} is not a value type");
            }

            try
            {
                return (Ops)Activator.CreateInstance(typeof(Ops<>).MakeGenericType(t));
            }
            catch (System.Reflection.TargetInvocationException tie) when (tie.InnerException != null)
            {
                throw tie.InnerException;
            }
        });
    }
}

} // namespace Clrpp
