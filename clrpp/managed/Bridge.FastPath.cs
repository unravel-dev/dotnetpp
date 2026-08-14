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
    // Long-lived pins for native pointer access (ArrayPinAcquire): one pinned
    // GCHandle per array strong-handle key, released on ArrayPinRelease /
    // FreeHandle / domain unload. Entries are claimed for release with an
    // atomic flag so a pin is freed exactly once, from whichever thread
    // releases it. GCHandle structs are never copied into thread-local or
    // per-caller state: a stale copy can alias a recycled handle slot and
    // read/write an unrelated object. Readers use the data pointer cached at
    // creation (pinned objects do not move) and never touch the GCHandle.
    //
    // Bulk copies (ArrayCopyTo/From) reuse a long-lived pin when one exists
    // and otherwise pin transiently for the duration of the single copy - a
    // scope-local pin is cheaper than registry churn for one-shot temp
    // arrays and cannot leak or alias.
    // ---------------------------------------------------------------------

    private sealed class ArrayPin
    {
        public GCHandle Pin;
        public IntPtr Data;
        public int ElementSize;
        public long ByteLength;
        private int released;

        public bool IsLive => System.Threading.Volatile.Read(ref released) == 0;

        /// Frees the pin exactly once; safe to race from multiple threads.
        public void Release()
        {
            if (System.Threading.Interlocked.Exchange(ref released, 1) == 0)
            {
                Pin.Free();
            }
        }
    }

    private static readonly System.Collections.Concurrent.ConcurrentDictionary<IntPtr, ArrayPin>
        ArrayPins = new();

    private static class ArrayPinCache
    {
        /// Registry lookup only (no creation); used by the bulk-copy paths so
        /// they cooperate with pins native already holds pointers into.
        internal static bool TryGetLive(IntPtr arrayHandle, out ArrayPin pin)
        {
            return ArrayPins.TryGetValue(arrayHandle, out pin) && pin.IsLive;
        }

        // Bulk-copy promotion heuristic: the first copy for a handle pins
        // transiently; a repeat copy of the same handle promotes into the
        // registry so steady-state repeated copies reuse one pin. Only an
        // IntPtr is cached per thread - never GCHandles or object refs, so a
        // stale value can at worst promote a fresh array one copy early.
        [ThreadStatic] private static IntPtr s_lastCopyHandle;

        internal static bool ShouldPromote(IntPtr arrayHandle)
        {
            if (s_lastCopyHandle == arrayHandle)
            {
                return true;
            }

            s_lastCopyHandle = arrayHandle;
            return false;
        }

        internal static bool TryAcquire(IntPtr arrayHandle, Array array, out ArrayPin pin)
        {
            while (true)
            {
                if (ArrayPins.TryGetValue(arrayHandle, out pin))
                {
                    if (pin.IsLive)
                    {
                        return true;
                    }

                    // Released concurrently; drop exactly that dead entry
                    // (a plain TryRemove could take out a newer live pin).
                    ((System.Collections.Generic.ICollection<System.Collections.Generic.KeyValuePair<IntPtr, ArrayPin>>)ArrayPins)
                        .Remove(new System.Collections.Generic.KeyValuePair<IntPtr, ArrayPin>(arrayHandle, pin));
                    continue;
                }

                var elementType = array.GetType().GetElementType();
                if (elementType == null || !ClrLayout.IsBlittable(elementType))
                {
                    pin = null;
                    return false;
                }

                ArrayPin fresh;
                try
                {
                    var size = ClrLayout.SizeOf(elementType);
                    var handle = GCHandle.Alloc(array, GCHandleType.Pinned);
                    fresh = new ArrayPin
                    {
                        Pin = handle,
                        Data = handle.AddrOfPinnedObject(),
                        ElementSize = size,
                        ByteLength = array.LongLength * size
                    };
                }
                catch (ArgumentException)
                {
                    // Element type not pinnable (contains references).
                    pin = null;
                    return false;
                }

                if (ArrayPins.TryAdd(arrayHandle, fresh))
                {
                    pin = fresh;
                    return true;
                }

                // Lost an insert race: free ours and retry with the winner's.
                fresh.Release();
            }
        }

        internal static void Release(IntPtr arrayHandle)
        {
            // No emptiness pre-check: ConcurrentDictionary.IsEmpty acquires
            // every bucket lock when the dictionary IS empty, which made this
            // (called from FreeHandle, i.e. per released object) an order of
            // magnitude slower than the single-bucket TryRemove miss.
            if (ArrayPins.TryRemove(arrayHandle, out var pin))
            {
                pin.Release();
            }
        }

        /// <summary>
        /// Drop every array pin. Required before ALC unload so pinned handles
        /// cannot root collectible assemblies.
        /// </summary>
        internal static void ReleaseAll()
        {
            var keys = ArrayPins.Keys.ToArray();
            if (keys.Length > 0)
            {
                Log($"releasing {keys.Length} array pin(s) before domain unload", "trace");
            }

            foreach (var key in keys)
            {
                Release(key);
            }
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

        if (!ArrayPinCache.TryAcquire(arrayHandle, array, out var pin))
        {
            return 0;
        }

        info->Data = pin.Data;
        info->ByteLength = pin.ByteLength;
        info->ElementSize = pin.ElementSize;
        info->Reserved = 0;
        return 1;
    }

    [UnmanagedCallersOnly]
    public static void ArrayPinRelease(IntPtr arrayHandle)
    {
        ArrayPinCache.Release(arrayHandle);
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
        /// Per-parameter CLR sizes for the blittable path, so the dispatch
        /// gate can reject undersized native blobs before any unchecked read.
        public int[] BlittableArgSizes;
    }

    private static readonly ConditionalWeakTable<MethodBase, MethodInvokePlan> MethodPlans = new();

    // Exact-length pools: MethodBase.Invoke / MethodInvoker require argc == Length.
    [ThreadStatic] private static object[][] s_invokeArgsByArity;

    // Tracks which pooled arity slots are currently rented on this thread so a
    // reentrant invoke (managed callee calling back into the bridge) does not
    // clobber a buffer still in use by the outer call.
    [ThreadStatic] private static bool[] s_invokeArgsInUse;

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
                    plan.BlittableArgSizes = Array.ConvertAll(
                        plan.Parameters, static p => ClrLayout.SizeOf(p.ParameterType));
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

        var inUse = s_invokeArgsInUse;
        if (inUse == null)
        {
            inUse = new bool[MaxCachedInvokeArity + 1];
            s_invokeArgsInUse = inUse;
        }

        // High-arity calls share a single oversized slot (index 0).
        var slot = argc <= MaxCachedInvokeArity ? argc : 0;

        // Reentrant use of the same slot on this thread: the pooled buffer is
        // still live for the outer invoke, so return a fresh, non-pooled array
        // rather than overwriting it.
        if (inUse[slot])
        {
            return new object[argc];
        }

        object[] buffer;
        if (slot != 0)
        {
            buffer = pools[slot];
            if (buffer == null)
            {
                buffer = new object[argc];
                pools[slot] = buffer;
            }
        }
        else
        {
            buffer = pools[0];
            if (buffer == null || buffer.Length != argc)
            {
                buffer = new object[argc];
                pools[0] = buffer;
            }
        }

        inUse[slot] = true;
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

        // Only release the slot when returning the pooled buffer itself;
        // reentrant calls received fresh arrays that were never pooled.
        var pools = s_invokeArgsByArity;
        var inUse = s_invokeArgsInUse;
        if (pools == null || inUse == null)
        {
            return;
        }

        var slot = argc <= MaxCachedInvokeArity ? argc : 0;
        if (ReferenceEquals(pools[slot], buffer))
        {
            inUse[slot] = false;
        }
    }

    // Field get/set: Bridge.FieldAccess*.cs (Compiled → Portable).
}

} // namespace Clrpp
