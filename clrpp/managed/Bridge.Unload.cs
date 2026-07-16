using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

namespace Clrpp
{

public static partial class Bridge
{
    // ---------------------------------------------------------------------
    // Domain unload with statics cleanup and leak detection.
    //
    // Mono destroys an AppDomain wholesale - statics, instances, everything.
    // A collectible AssemblyLoadContext only goes away once nothing
    // references it anymore, so a single static field in a *surviving*
    // context (e.g. an engine-side manager caching script instances or
    // script Types) silently keeps the whole context alive forever.
    //
    // DomainUnload therefore:
    //   1. runs [AutoStaticsCleanup] over every clrpp context: types marked
    //      with the attribute either get `static OnStaticsCleanup()` invoked
    //      (when defined) or all their non-readonly static fields reset,
    //   2. purges interned reflection handles owned by the dying context,
    //   3. unloads and *verifies* collection through a WeakReference,
    //   4. when the context survives, scans the static fields of the live
    //      contexts for roots into the dead one and reports every finding.
    //
    // The verification itself is cheap (a WeakReference probe after GC); the
    // diagnostic scan only runs when a leak was actually detected.
    // ---------------------------------------------------------------------

    private const string CleanupAttributeName = "AutoStaticsCleanupAttribute";
    private const string CleanupMethodName = "OnStaticsCleanup";

    /// <returns>0 = unloaded cleanly, 1 = context leaked, -1 = error.</returns>
    [UnmanagedCallersOnly]
    public static int DomainUnload(IntPtr domainHandle)
    {
        try
        {
            var weak = PrepareAndUnload(domainHandle, out var name, out var identity);
            if (weak == null)
            {
                return -1;
            }

            // Give the collectible context a fair chance to actually unload.
            for (int i = 0; i < 8 && weak.IsAlive; i++)
            {
                GC.Collect();
                GC.WaitForPendingFinalizers();
            }

            if (!weak.IsAlive)
            {
                Log($"domain '{name}' unloaded cleanly", "debug");
                return 0;
            }

            DiagnoseLeak(weak, name, identity);
            return 1;
        }
        catch (Exception ex)
        {
            Log($"DomainUnload failed: {ex}", "error");
            return -1;
        }
    }

    // Kept out of DomainUnload so no local in the caller's frame roots the
    // context while the GC verifies collection.
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference PrepareAndUnload(IntPtr domainHandle,
                                                  out string name,
                                                  out LeakAnalysis.ContextIdentity identity)
    {
        name = null;
        identity = null;
        var alc = Target<ClrppLoadContext>(domainHandle);
        if (alc == null)
        {
            return null;
        }
        name = alc.Name;

        // Captured up front: after Unload() the context must not be touched
        // again, but the leak analysis still needs to know what lived in it.
        identity = LeakAnalysis.ContextIdentity.Capture(alc);

        RunStaticsCleanup();
        PurgeInternedHandles(alc);
        ArrayPinCache.ReleaseAllLongLived();

        GCHandle.FromIntPtr(domainHandle).Free();

        var weak = new WeakReference(alc);
        alc.Unload();
        return weak;
    }

    // -- statics cleanup ----------------------------------------------------

    private static void RunStaticsCleanup()
    {
        foreach (var context in AssemblyLoadContext.All.OfType<ClrppLoadContext>())
        {
            foreach (var assembly in context.Assemblies)
            {
                foreach (var type in SafeGetTypes(assembly))
                {
                    if (!HasCleanupAttribute(type))
                    {
                        continue;
                    }

                    try
                    {
                        CleanupStatics(type);
                    }
                    catch (Exception ex)
                    {
                        Log($"[AutoStaticsCleanup] cleanup failed for {type.FullName}: {ex.Message}", "warning");
                    }
                }
            }
        }
    }

    // Attribute matched by name so any assembly can define its own copy
    // (user assemblies do not reference the bridge at compile time).
    // GetCustomAttributesData avoids instantiating the attribute.
    private static bool HasCleanupAttribute(Type type)
    {
        try
        {
            foreach (var attribute in type.GetCustomAttributesData())
            {
                if (attribute.AttributeType.Name == CleanupAttributeName)
                {
                    return true;
                }
            }
        }
        catch
        {
            // metadata of a broken/partially-loaded type - not a candidate
        }

        return false;
    }

    private static void CleanupStatics(Type type)
    {
        // A type can take over its own reset (e.g. re-create managers instead
        // of nulling them) by defining `static void OnStaticsCleanup()`.
        var custom = type.GetMethod(CleanupMethodName,
                                    BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic |
                                        BindingFlags.DeclaredOnly,
                                    binder: null, Type.EmptyTypes, modifiers: null);
        if (custom != null)
        {
            custom.Invoke(null, null);
            return;
        }

        var fields = type.GetFields(BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic |
                                    BindingFlags.DeclaredOnly);
        foreach (var field in fields)
        {
            if (field.IsLiteral)
            {
                continue; // const
            }

            if (field.IsInitOnly)
            {
                // CoreCLR forbids reflection writes to static readonly fields.
                Log($"[AutoStaticsCleanup] cannot clear readonly field {type.FullName}.{field.Name} " +
                    $"- implement `static void {CleanupMethodName}()` on the type instead",
                    "warning");
                continue;
            }

            field.SetValue(null, field.FieldType.IsValueType ? Activator.CreateInstance(field.FieldType) : null);
        }
    }

    // -- interned handle purge ------------------------------------------------

    // Interned handles (Type/MethodInfo/... - see Intern) are session-lifetime
    // strong GCHandles. Any of them targeting a reflection object of the dying
    // context would pin it forever, so they are released here. The native side
    // resets its corresponding caches right after the unload call.
    private static void PurgeInternedHandles(AssemblyLoadContext context)
    {
        foreach (var entry in InternedHandles)
        {
            if (!BelongsToContext(entry.Key, context))
            {
                continue;
            }

            if (InternedHandles.TryRemove(entry.Key, out var ptr) &&
                InternedHandlesReverse.TryRemove(ptr, out var gc))
            {
                gc.Free();
            }
        }
    }

    // -- context ownership ----------------------------------------------------

    private static bool BelongsToContext(object obj, AssemblyLoadContext context)
    {
        return obj switch
        {
            null => false,
            AssemblyLoadContext alc => alc == context,
            Assembly assembly => AssemblyLoadContext.GetLoadContext(assembly) == context,
            Type type => TypeBelongsToContext(type, context),
            MemberInfo member => MemberBelongsToContext(member, context),
            Delegate del => DelegateBelongsToContext(del, context),
            _ => TypeBelongsToContext(obj.GetType(), context),
        };
    }

    // Both sides matter: a member declared on an engine base class but
    // reflected through an app type (typeof(AppScript).GetProperty(...) for
    // an inherited property) has an engine DeclaringType, yet its
    // m_reflectedTypeCache keeps the app RuntimeType - and therefore the
    // whole app context - alive.
    private static bool MemberBelongsToContext(MemberInfo member, AssemblyLoadContext context)
    {
        return (member.DeclaringType != null && TypeBelongsToContext(member.DeclaringType, context)) ||
               (member.ReflectedType != null && TypeBelongsToContext(member.ReflectedType, context));
    }

    private static bool TypeBelongsToContext(Type type, AssemblyLoadContext context)
    {
        if (type.HasElementType)
        {
            return TypeBelongsToContext(type.GetElementType(), context);
        }

        if (AssemblyLoadContext.GetLoadContext(type.Assembly) == context)
        {
            return true;
        }

        if (type.IsConstructedGenericType)
        {
            foreach (var arg in type.GetGenericArguments())
            {
                if (TypeBelongsToContext(arg, context))
                {
                    return true;
                }
            }
        }

        return false;
    }

    private static bool DelegateBelongsToContext(Delegate del, AssemblyLoadContext context)
    {
        foreach (var d in del.GetInvocationList())
        {
            if (d.Target != null && BelongsToContext(d.Target, context))
            {
                return true;
            }

            if (d.Method?.DeclaringType != null && TypeBelongsToContext(d.Method.DeclaringType, context))
            {
                return true;
            }
        }

        return false;
    }

    // -- leak diagnostics -------------------------------------------------------

    private static void DiagnoseLeak(WeakReference weak, string name, LeakAnalysis.ContextIdentity identity)
    {
        if (!weak.IsAlive)
        {
            Log($"domain '{name}' unloaded cleanly (collected during diagnosis)", "debug");
            return;
        }

        Log($"domain '{name}' LEAKED - the AssemblyLoadContext is still reachable after unload", "error");

        // Kept in its own frame: the GC snapshot below must not see a strong
        // reference to the dead context from this thread's stack.
        int roots = ScanStaticRoots(weak);

        // Ask the GC itself: snapshot the process and walk the real handle
        // table, heap and root paths (see Bridge.LeakAnalysis.cs).
        roots += LeakAnalysis.Run(identity, name);

        if (roots == 0)
        {
            Log("  no roots found by the quick scans. Likely causes: running threads/timers " +
                "executing code from the domain, or roots only visible to a full memory profiler.",
                "error");
        }

        // Unloading is asynchronous under the hood: the runtime holds its own
        // strong handle to the context until the LoaderAllocator teardown
        // completes, which can take extra GC/finalizer rounds. Re-verify so a
        // slow-but-successful unload is not mistaken for a permanent leak.
        for (int i = 0; i < 4 && weak.IsAlive; i++)
        {
            GC.Collect();
            GC.WaitForPendingFinalizers();
        }

        if (!weak.IsAlive)
        {
            Log($"domain '{name}' collected after additional GC rounds - the unload was slow, " +
                "not leaked (the report above shows what delayed it)",
                "warning");
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int ScanStaticRoots(WeakReference weak)
    {
        // Temporary strong reference for the duration of the scan.
        if (weak.Target is not AssemblyLoadContext dead)
        {
            return 0;
        }

        int roots = 0;
        foreach (var context in AssemblyLoadContext.All.OfType<ClrppLoadContext>())
        {
            if (context == dead)
            {
                continue;
            }

            foreach (var assembly in context.Assemblies)
            {
                foreach (var type in SafeGetTypes(assembly))
                {
                    foreach (var field in SafeGetStaticFields(type))
                    {
                        var description = DescribeRoot(field, dead);
                        if (description != null)
                        {
                            roots++;
                            Log($"  static root: {type.FullName}.{field.Name} {description}", "error");
                        }
                    }
                }
            }
        }

        return roots;
    }

    private static FieldInfo[] SafeGetStaticFields(Type type)
    {
        try
        {
            return type.GetFields(BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic |
                                  BindingFlags.DeclaredOnly);
        }
        catch
        {
            return Array.Empty<FieldInfo>();
        }
    }

    private static string DescribeRoot(FieldInfo field, AssemblyLoadContext dead)
    {
        object value;
        try
        {
            // May run the type's cctor; acceptable in the (rare) leak path.
            value = field.GetValue(null);
        }
        catch
        {
            return null;
        }

        if (value == null)
        {
            return null;
        }

        if (BelongsToContext(value, dead))
        {
            return $"-> {value.GetType().FullName}";
        }

        // Shallow scan of collection contents; caches keyed by leaked Type
        // objects (e.g. Dictionary<Type, ...>) are a very common root.
        if (value is IEnumerable enumerable && value is not string)
        {
            try
            {
                int inspected = 0;
                foreach (var element in enumerable)
                {
                    if (++inspected > 128)
                    {
                        break;
                    }

                    if (element == null)
                    {
                        continue;
                    }

                    if (BelongsToContext(element, dead))
                    {
                        return $"contains {element.GetType().FullName}";
                    }

                    if (TryGetPairParts(element, out var key, out var val))
                    {
                        if (key != null && BelongsToContext(key, dead))
                        {
                            return $"contains key {(key as Type)?.FullName ?? key.GetType().FullName}";
                        }

                        if (val != null && BelongsToContext(val, dead))
                        {
                            return $"contains value {val.GetType().FullName}";
                        }
                    }
                }
            }
            catch
            {
                // enumeration side effects/failures - skip this field
            }
        }

        return null;
    }

    private static bool TryGetPairParts(object element, out object key, out object value)
    {
        if (element is DictionaryEntry entry)
        {
            key = entry.Key;
            value = entry.Value;
            return true;
        }

        var type = element.GetType();
        if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(KeyValuePair<,>))
        {
            key = type.GetProperty("Key")?.GetValue(element);
            value = type.GetProperty("Value")?.GetValue(element);
            return true;
        }

        key = null;
        value = null;
        return false;
    }

}

} // namespace Clrpp
