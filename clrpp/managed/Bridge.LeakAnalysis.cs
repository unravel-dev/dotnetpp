using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Text;
using Microsoft.Diagnostics.Runtime;

namespace Clrpp
{

/// <summary>
/// Post-mortem analysis for a leaked (failed to unload) AssemblyLoadContext,
/// powered by ClrMD (Microsoft.Diagnostics.Runtime).
///
/// Instead of tracking GCHandles by hand at runtime (which would tax every
/// hot icall), this asks the GC itself: the process is snapshotted
/// (PssCaptureSnapshot on Windows - the live process is only suspended for
/// the duration of the snapshot call) and the *real* GC handle table, heap
/// and root paths are inspected. The cost is paid exclusively when a leak
/// was already detected; steady-state overhead is zero.
///
/// Reports, in order:
///   1. GC handles (strong/pinned/...) that still target objects from the
///      leaked context - these are typically handles native code owns and
///      forgot to release,
///   2. a census of surviving instances from the leaked assemblies,
///   3. full root paths (static field / handle / stack -> ... -> object)
///      for a few sample leaked objects.
///
/// Optional dependency: requires Microsoft.Diagnostics.Runtime.dll next to
/// the bridge; when missing, the analysis is skipped with a hint.
/// </summary>
internal static class LeakAnalysis
{
    /// <summary>
    /// Identity of a context captured *before* Unload() is initiated, so the
    /// analysis does not need to touch the dying context (and cannot
    /// accidentally root it).
    /// </summary>
    internal sealed class ContextIdentity
    {
        public List<string> AssemblyNames = new();
        public HashSet<string> TypeNames = new(StringComparer.Ordinal);

        public static ContextIdentity Capture(AssemblyLoadContext context)
        {
            var identity = new ContextIdentity();
            try
            {
                foreach (var assembly in context.Assemblies)
                {
                    var name = assembly.GetName().Name;
                    if (!string.IsNullOrEmpty(name))
                    {
                        identity.AssemblyNames.Add(name);
                    }

                    foreach (var type in Bridge.SafeGetTypes(assembly))
                    {
                        // ClrMD reports nested types as Ns.Outer+Nested, same
                        // as reflection FullName.
                        if (type.FullName != null)
                        {
                            identity.TypeNames.Add(type.FullName);
                        }
                    }
                }
            }
            catch
            {
                // best effort - partial identity still matches most objects
            }

            return identity;
        }
    }

    /// <summary>Runs the analysis. Returns the number of findings reported.</summary>
    internal static int Run(ContextIdentity identity, string domainName)
    {
        if (identity == null || identity.TypeNames.Count == 0)
        {
            return 0;
        }

        try
        {
            return RunCore(identity, domainName);
        }
        catch (FileNotFoundException ex) when (ex.FileName != null &&
                                               ex.FileName.Contains("Microsoft.Diagnostics.Runtime"))
        {
            Bridge.Log("  GC snapshot analysis skipped: Microsoft.Diagnostics.Runtime.dll not found " +
                       "next to the bridge assembly", "warning");
            return 0;
        }
        catch (Exception ex)
        {
            Bridge.Log($"  GC snapshot analysis failed: {ex.Message}", "warning");
            return 0;
        }
    }

    // Separate method so ClrMD only has to load when a leak actually occurred.
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int RunCore(ContextIdentity identity, string domainName)
    {
        var started = Environment.TickCount64;

        using var dataTarget = DataTarget.CreateSnapshotAndAttach(Environment.ProcessId);
        using var runtime = dataTarget.ClrVersions[0].CreateRuntime();
        var heap = runtime.Heap;
        if (!heap.CanWalkHeap)
        {
            Bridge.Log("  GC snapshot analysis skipped: heap is not in a walkable state", "warning");
            return 0;
        }

        int findings = 0;
        var sampleObjects = new List<ulong>();

        // -- 1. GC handle table -------------------------------------------------
        // A strong/pinning handle to a leaked object is almost always a handle
        // the native side owns and did not free (clrpp object handles, pinned
        // script instances). Handles to System.RuntimeType are decoded so a
        // leaked typeof(MyScript) is recognized even though the RuntimeType
        // object itself lives in CoreLib.
        var handleCounts = new Dictionary<string, int>();
        foreach (var handle in runtime.EnumerateHandles())
        {
            if (!IsRootingKind(handle.HandleKind))
            {
                continue;
            }

            var display = DescribeLeakedTarget(handle.Object, identity);
            if (display == null)
            {
                continue;
            }

            var key = $"{handle.HandleKind} handle -> {display}";
            handleCounts[key] = handleCounts.GetValueOrDefault(key) + 1;

            if (sampleObjects.Count < 4)
            {
                sampleObjects.Add(handle.Object.Address);
            }
        }

        foreach (var kv in handleCounts.OrderByDescending(kv => kv.Value))
        {
            findings++;
            Bridge.Log($"  gc handle root: {kv.Value}x {kv.Key}", "error");
        }

        // -- 2. Heap walk: instances + leaked reflection objects ------------------
        // Objects that root the context without carrying its type identity are
        // decoded explicitly:
        //   - RuntimeType: typeof(MyScript) is a CoreLib object,
        //   - delegates: a cached Action over an app method is a CoreLib
        //     object whose method table entry pins the app LoaderAllocator.
        var instanceCounts = new Dictionary<string, int>();
        long leakedInstances = 0;
        foreach (var obj in heap.EnumerateObjects())
        {
            var type = obj.Type;
            if (type == null)
            {
                continue;
            }

            string display = null;
            if (IsLeakedType(type, identity))
            {
                display = type.Name;
            }
            else if (type.Name == "System.RuntimeType")
            {
                display = DescribeLeakedTarget(obj, identity);
            }
            else if (obj.IsDelegate)
            {
                display = DescribeLeakedDelegate(obj, identity);
            }

            if (display == null)
            {
                continue;
            }

            leakedInstances++;
            instanceCounts[display] = instanceCounts.GetValueOrDefault(display) + 1;

            if (sampleObjects.Count < 8)
            {
                sampleObjects.Add(obj.Address);
            }
        }

        if (leakedInstances > 0)
        {
            findings++;
            Bridge.Log($"  {leakedInstances} live object(s) from '{domainName}' assemblies " +
                       $"({string.Join(", ", identity.AssemblyNames)}):", "error");
            foreach (var kv in instanceCounts.OrderByDescending(kv => kv.Value).Take(15))
            {
                Bridge.Log($"    {kv.Value}x {kv.Key}", "error");
            }
        }

        // -- 3. Threads still executing code from the leaked assemblies -----------
        foreach (var thread in runtime.Threads)
        {
            try
            {
                foreach (var frame in thread.EnumerateStackTrace())
                {
                    var frameType = frame.Method?.Type;
                    if (frameType != null && IsLeakedType(frameType, identity))
                    {
                        findings++;
                        Bridge.Log($"  thread root: thread {thread.OSThreadId} is executing " +
                                   $"{frameType.Name}.{frame.Method.Name}", "error");
                        break;
                    }
                }
            }
            catch
            {
                // unwalkable stack - skip
            }
        }

        // -- 4. Root paths ---------------------------------------------------------
        // Shows the exact chain that keeps the sample leaked objects alive:
        //   <root kind> RootType.field -> Holder.field -> ... -> Leaked
        if (sampleObjects.Count > 0)
        {
            var gcroot = new GCRoot(heap, sampleObjects.Distinct().ToArray());
            var seen = new HashSet<string>();
            int internalPaths = 0;
            foreach ((ClrRoot root, GCRoot.ChainLink path) in gcroot.EnumerateRootPaths())
            {
                // While an unload is pending, the runtime itself holds a strong
                // handle to the dying context, and the context reaches its own
                // RuntimeTypes through the LoaderAllocator. Those chains are
                // expected, not leaks.
                if (root.Object.Type?.Name == "Clrpp.ClrppLoadContext" ||
                    root.Object.Type?.Name == "System.Reflection.LoaderAllocator")
                {
                    internalPaths++;
                    continue;
                }

                var line = FormatRootPath(heap, root, path);
                if (!seen.Add(line))
                {
                    continue;
                }

                findings++;
                Bridge.Log($"  root path: {line}", "error");

                if (seen.Count >= 8)
                {
                    break;
                }
            }

            if (seen.Count > 0)
            {
                Bridge.Log("  (handle roots are GCHandles - when strong/pinned, typically owned by " +
                           "native code; a pinned System.Object[] usually carries static variables)",
                           "info");
            }
            else if (internalPaths > 0)
            {
                Bridge.Log("  only runtime-internal root paths found (pending-unload handle / " +
                           "LoaderAllocator) - the unload is likely still in flight rather than leaked",
                           "info");
            }
        }
        else if (findings == 0)
        {
            // Note: a strong handle to the ClrppLoadContext object itself is
            // expected while its unload is pending - the runtime holds one
            // internally until the LoaderAllocator is actually released, so it
            // is deliberately not reported as a root here.
            Bridge.Log("  no leaked instances, reflection objects or thread frames found - the " +
                       "context is likely pinned by something only a full memory profiler " +
                       "(dotMemory / PerfView) can attribute (e.g. a native LoaderAllocator " +
                       "reference from JITted code)", "warning");
        }

        Bridge.Log($"  GC snapshot analysis finished in {Environment.TickCount64 - started} ms", "info");
        return findings;
    }

    /// <summary>
    /// Returns a display string when the handle target belongs to the leaked
    /// context (directly, or as the RuntimeType of a leaked type), else null.
    /// </summary>
    private static string DescribeLeakedTarget(ClrObject obj, ContextIdentity identity)
    {
        var type = obj.Type;
        if (type == null)
        {
            return null;
        }

        if (IsLeakedType(type, identity))
        {
            return type.Name;
        }

        if (type.Name == "System.RuntimeType")
        {
            try
            {
                var represented = obj.AsRuntimeType();
                if (represented != null && IsLeakedType(represented, identity))
                {
                    return $"typeof({represented.Name})";
                }
            }
            catch
            {
                // undecodable RuntimeType - not attributable to the context
            }
        }

        return null;
    }

    /// <summary>
    /// Returns a display string when the delegate points at a method defined
    /// in the leaked context, else null. The delegate object itself is
    /// usually a CoreLib type (Action/Func), so type matching misses it.
    /// </summary>
    private static string DescribeLeakedDelegate(ClrObject obj, ContextIdentity identity)
    {
        try
        {
            foreach (var target in obj.AsDelegate().EnumerateDelegateTargets())
            {
                var declaring = target.Method?.Type;
                if (declaring != null && IsLeakedType(declaring, identity))
                {
                    return $"{obj.Type.Name} -> {declaring.Name}.{target.Method.Name}";
                }
            }
        }
        catch
        {
            // undecodable delegate - not attributable to the context
        }

        return null;
    }

    private static string FormatRootPath(ClrHeap heap, ClrRoot root, GCRoot.ChainLink path)
    {
        var line = new StringBuilder();
        var previous = root.Object;
        line.Append(root.RootKind).Append(' ').Append(previous.Type?.Name ?? "<unknown>");

        for (var link = path; link != null; link = link.Next)
        {
            if (link.Object == previous.Address)
            {
                continue;
            }

            var current = heap.GetObject(link.Object);
            line.Append(FindFieldName(previous, current.Address));
            line.Append(" -> ").Append(current.Type?.Name ?? "<unknown>");
            previous = current;
        }

        return line.ToString();
    }

    /// <summary>".fieldName" when the parent references the child through a
    /// named instance field, otherwise an empty string (array element etc).</summary>
    private static string FindFieldName(ClrObject parent, ulong child)
    {
        try
        {
            foreach (var reference in parent.EnumerateReferencesWithFields(carefully: true))
            {
                if (reference.Object.Address == child && reference.Field != null)
                {
                    return "." + reference.Field.Name;
                }
            }
        }
        catch
        {
            // field resolution is best-effort decoration only
        }

        return string.Empty;
    }

    private static bool IsRootingKind(ClrHandleKind kind)
    {
        return kind is ClrHandleKind.Strong or ClrHandleKind.Pinned or ClrHandleKind.AsyncPinned or
               ClrHandleKind.RefCounted or ClrHandleKind.SizedRef;
    }

    private static bool IsLeakedType(ClrType type, ContextIdentity identity)
    {
        var name = type.Name;
        if (name != null && identity.TypeNames.Contains(name))
        {
            return true;
        }

        // Fallback: match by module (works when assemblies were loaded from
        // disk; stream-loaded modules may have no name).
        var moduleName = type.Module?.Name;
        if (!string.IsNullOrEmpty(moduleName))
        {
            var file = Path.GetFileNameWithoutExtension(moduleName);
            foreach (var assemblyName in identity.AssemblyNames)
            {
                if (string.Equals(file, assemblyName, StringComparison.OrdinalIgnoreCase))
                {
                    return true;
                }
            }
        }

        return false;
    }
}

} // namespace Clrpp
