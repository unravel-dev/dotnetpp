using System;
using System.Linq;
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
    // Array pin cache
    // ---------------------------------------------------------------------
    //
    // Two layers:
    //   1) Thread-local pin reused by ArrayCopyTo/From (short-lived, one
    //      array at a time on this thread).
    //   2) Long-lived pins keyed by the array's strong-handle IntPtr, used by
    //      ArrayPinAcquire so native clr_array get/set can memcpy without
    //      re-entering the bridge. Released on ArrayPinRelease / FreeHandle.
    // ---------------------------------------------------------------------

    private sealed class LongLivedArrayPin
    {
        public GCHandle Pin;
        public int ElementSize;
        public long ByteLength;
    }

    private static readonly System.Collections.Concurrent.ConcurrentDictionary<IntPtr, LongLivedArrayPin>
        LongLivedPins = new();

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

            // Prefer an already-acquired long-lived pin for this handle so we
            // do not double-pin the same array on the copy path.
            if (LongLivedPins.TryGetValue(arrayHandle, out var lived) && lived.Pin.IsAllocated)
            {
                // Drop any TLS-owned pin before aliasing the long-lived one.
                if (s_handle != arrayHandle || !IsAliasingLongLived())
                {
                    ReleaseCurrent();
                }

                s_handle = arrayHandle;
                s_pin = lived.Pin;
                s_elementSize = lived.ElementSize;
                s_byteLength = lived.ByteLength;
                return s_pin;
            }

            ReleaseCurrent();
            s_pin = GCHandle.Alloc(array, GCHandleType.Pinned);
            s_handle = arrayHandle;
            s_elementSize = 0;
            s_byteLength = -1;
            return s_pin;
        }

        private static bool IsAliasingLongLived()
        {
            return s_handle != IntPtr.Zero && LongLivedPins.ContainsKey(s_handle);
        }

        private static void SafeFreePin(ref GCHandle pin)
        {
            if (!pin.IsAllocated)
            {
                return;
            }

            try
            {
                pin.Free();
            }
            catch (InvalidOperationException)
            {
                // Already freed on another path (TLS alias vs long-lived).
            }

            pin = default;
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
            var livedPin = default(GCHandle);
            var hadLongLived = LongLivedPins.TryGetValue(arrayHandle, out var lived);
            if (hadLongLived)
            {
                livedPin = lived.Pin;
            }

            ReleaseLongLived(arrayHandle);

            if (s_handle != arrayHandle)
            {
                return;
            }

            // ReleaseLongLived usually clears TLS; if not (e.g. missed), finish here.
            if (s_pin.IsAllocated && (hadLongLived && s_pin.Equals(livedPin) || IsAliasingLongLived()))
            {
                s_handle = IntPtr.Zero;
                s_pin = default;
                s_elementSize = 0;
                s_byteLength = -1;
            }
            else
            {
                ReleaseCurrent();
            }
        }

        internal static void ReleaseCurrent()
        {
            // Only free when TLS owns the pin; aliases of long-lived pins must not.
            if (s_pin.IsAllocated && !IsAliasingLongLived())
            {
                SafeFreePin(ref s_pin);
            }

            s_handle = IntPtr.Zero;
            s_pin = default;
            s_elementSize = 0;
            s_byteLength = -1;
        }

        internal static bool TryAcquireLongLived(IntPtr arrayHandle, Array array,
                                                 out IntPtr data, out int elementSize, out long byteLength)
        {
            data = IntPtr.Zero;
            elementSize = 0;
            byteLength = 0;

            var elementType = array.GetType().GetElementType();
            if (elementType == null || !ClrLayout.IsBlittable(elementType))
            {
                return false;
            }

            // ArrayCopy may already hold a TLS-owned pin for this handle. Drop it
            // before allocating a long-lived pin so FreeHandle cannot orphan the TLS one.
            if (s_handle == arrayHandle && s_pin.IsAllocated && !IsAliasingLongLived())
            {
                ReleaseCurrent();
            }

            var lived = LongLivedPins.GetOrAdd(arrayHandle, _ =>
            {
                var size = ClrLayout.SizeOf(elementType);
                return new LongLivedArrayPin
                {
                    Pin = GCHandle.Alloc(array, GCHandleType.Pinned),
                    ElementSize = size,
                    ByteLength = array.LongLength * size
                };
            });

            if (!lived.Pin.IsAllocated)
            {
                LongLivedPins.TryRemove(arrayHandle, out _);
                return false;
            }

            // TLS should alias the long-lived pin (not a second owned pin).
            s_handle = arrayHandle;
            s_pin = lived.Pin;
            s_elementSize = lived.ElementSize;
            s_byteLength = lived.ByteLength;

            data = lived.Pin.AddrOfPinnedObject();
            elementSize = lived.ElementSize;
            byteLength = lived.ByteLength;
            return true;
        }

        internal static void ReleaseLongLived(IntPtr arrayHandle)
        {
            if (!LongLivedPins.TryRemove(arrayHandle, out var lived))
            {
                return;
            }

            if (s_handle == arrayHandle)
            {
                // Free a TLS-owned pin that is not the long-lived one (orphan guard).
                if (s_pin.IsAllocated && !s_pin.Equals(lived.Pin))
                {
                    SafeFreePin(ref s_pin);
                }

                s_handle = IntPtr.Zero;
                s_pin = default;
                s_elementSize = 0;
                s_byteLength = -1;
            }

            SafeFreePin(ref lived.Pin);
        }

        /// <summary>
        /// Drop every long-lived array pin. Required before ALC unload so
        /// pinned handles cannot root collectible assemblies.
        /// </summary>
        internal static void ReleaseAllLongLived()
        {
            var keys = LongLivedPins.Keys.ToArray();
            if (keys.Length > 0)
            {
                Log($"releasing {keys.Length} long-lived array pin(s) before domain unload", "trace");
            }

            foreach (var key in keys)
            {
                ReleaseLongLived(key);
            }

            LongLivedPins.Clear();
            ReleaseCurrent();
        }
    }

    /// <summary>
    /// Layout must match clr::clr_array_pin_info on the native side.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct NativeArrayPinInfo
    {
        public IntPtr Data;
        public long ByteLength;
        public int ElementSize;
        public int Reserved;
    }

    /// <summary>
    /// Pin a blittable-element array for native pointer access. The pin is
    /// released by ArrayPinRelease or FreeHandle (same strong-handle key).
    /// Returns 1 on success, 0 when the array is missing / non-blittable.
    /// </summary>
    [UnmanagedCallersOnly]
    public static unsafe int ArrayPinAcquire(IntPtr arrayHandle, NativeArrayPinInfo* info)
    {
        if (info == null || Target(arrayHandle) is not Array array)
        {
            return 0;
        }

        if (!ArrayPinCache.TryAcquireLongLived(arrayHandle, array, out var data, out var elemSize,
                                               out var byteLength))
        {
            return 0;
        }

        info->Data = data;
        info->ByteLength = byteLength;
        info->ElementSize = elemSize;
        info->Reserved = 0;
        return 1;
    }

    [UnmanagedCallersOnly]
    public static void ArrayPinRelease(IntPtr arrayHandle)
    {
        ArrayPinCache.ReleaseLongLived(arrayHandle);
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

    // Field get/set: Bridge.FieldAccess*.cs (Compiled → Portable).
}

} // namespace Clrpp
