using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

namespace Clrpp
{

internal sealed class ClrppLoadContext : AssemblyLoadContext
{
    private readonly List<string> searchPaths = new();

    // Assemblies loaded by any clrpp context, keyed by simple name. A child
    // context resolving a reference (e.g. the app assembly referencing the
    // engine assembly) reuses the already-loaded instance instead of loading
    // a private copy. Without this, each context would get its own copy of
    // the assembly with distinct type identities and separate statics -
    // unlike mono, where images are shared across domains.
    private static readonly object SharedAssembliesLock = new();
    private static readonly Dictionary<string, Assembly> SharedAssemblies = new(StringComparer.OrdinalIgnoreCase);

    public ClrppLoadContext(string name)
        : base(name, isCollectible: true)
    {
        Resolving += OnResolving;
        Unloading += _ => RemoveSharedAssembliesOwnedBy(this);
    }

    private static void RegisterShared(Assembly assembly)
    {
        var name = assembly.GetName().Name;
        if (string.IsNullOrEmpty(name))
        {
            return;
        }

        lock (SharedAssembliesLock)
        {
            // First-loaded wins; a later load of the same simple name in
            // another context keeps its private copy (explicit loads bypass
            // this table entirely).
            SharedAssemblies.TryAdd(name, assembly);
        }
    }

    private static void RemoveSharedAssembliesOwnedBy(ClrppLoadContext context)
    {
        lock (SharedAssembliesLock)
        {
            var stale = SharedAssemblies
                .Where(kv => GetLoadContext(kv.Value) == context)
                .Select(kv => kv.Key)
                .ToList();

            foreach (var key in stale)
            {
                SharedAssemblies.Remove(key);
            }
        }
    }

    public void AddSearchPath(string path)
    {
        lock (searchPaths)
        {
            if (!searchPaths.Contains(path))
            {
                searchPaths.Add(path);
            }
        }
    }

    private Assembly OnResolving(AssemblyLoadContext context, AssemblyName name)
    {
        // The bridge assembly must never be duplicated into a child context,
        // otherwise user assemblies would talk to a second, uninitialized
        // Bridge/InternalCalls instance.
        if (name.Name == typeof(Bridge).Assembly.GetName().Name)
        {
            return typeof(Bridge).Assembly;
        }

        // Reuse an assembly already loaded by another clrpp context so type
        // identities (and their statics) unify across contexts, matching
        // mono's shared-image behavior.
        lock (SharedAssembliesLock)
        {
            if (name.Name != null && SharedAssemblies.TryGetValue(name.Name, out var shared))
            {
                return shared;
            }
        }

        lock (searchPaths)
        {
            foreach (var dir in searchPaths)
            {
                var candidate = Path.Combine(dir, name.Name + ".dll");
                if (File.Exists(candidate))
                {
                    return LoadFromPath(candidate);
                }
            }
        }

        return null;
    }

    public Assembly LoadFromPath(string path)
    {
        // Load through a memory stream so the file on disk stays unlocked
        // (enables recompilation while the runtime is alive, mirroring the
        // mono backend's shadow-copy-free workflow).
        var fullPath = Path.GetFullPath(path);

        var assemblyBytes = File.ReadAllBytes(fullPath);
        var pdbPath = Path.ChangeExtension(fullPath, ".pdb");
        var pdbBytes = File.Exists(pdbPath) ? File.ReadAllBytes(pdbPath) : null;

        // Optionally rewrite mono-style [InternalCall] extern methods with
        // real bodies (see Weaver.cs). In-memory only; disk stays untouched.
        if (IcallWeaver.Enabled)
        {
            try
            {
                IcallWeaver.Weave(ref assemblyBytes, ref pdbBytes);
            }
            catch (FileNotFoundException ex) when (ex.FileName != null && ex.FileName.Contains("Mono.Cecil"))
            {
                // Cecil is not deployed - the feature is optional, turn it off.
                IcallWeaver.Enabled = false;
                Bridge.Log("icall weaving disabled: Mono.Cecil.dll not found next to the bridge", "warning");
            }
            catch (Exception ex)
            {
                Bridge.Log($"icall weaving failed for {Path.GetFileName(fullPath)}: {ex.Message}", "warning");
            }
        }

        using var assemblyStream = new MemoryStream(assemblyBytes);

        Assembly assembly;
        if (pdbBytes != null)
        {
            using var pdbStream = new MemoryStream(pdbBytes);
            assembly = LoadFromStream(assemblyStream, pdbStream);
        }
        else
        {
            assembly = LoadFromStream(assemblyStream);
        }

        RegisterShared(assembly);
        return assembly;
    }
}

public static partial class Bridge
{
    // ---------------------------------------------------------------------
    // Domains (AssemblyLoadContext)
    // ---------------------------------------------------------------------

    [UnmanagedCallersOnly]
    public static IntPtr DomainCreate(IntPtr nameUtf8)
    {
        try
        {
            var name = ReadUtf8(nameUtf8) ?? "clrpp_domain";
            var alc = new ClrppLoadContext(name);
            return NewObjectHandle(alc);
        }
        catch (Exception ex)
        {
            Log($"DomainCreate failed: {ex}", "error");
            return IntPtr.Zero;
        }
    }

    // DomainUnload (statics cleanup + leak detection) lives in Bridge.Unload.cs.

    [UnmanagedCallersOnly]
    public static IntPtr DomainGetName(IntPtr domainHandle)
    {
        var alc = Target<ClrppLoadContext>(domainHandle);
        return AllocUtf8(alc?.Name ?? string.Empty);
    }

    [UnmanagedCallersOnly]
    public static void DomainAddSearchPath(IntPtr domainHandle, IntPtr pathUtf8)
    {
        var alc = Target<ClrppLoadContext>(domainHandle);
        var path = ReadUtf8(pathUtf8);
        if (alc != null && !string.IsNullOrEmpty(path))
        {
            alc.AddSearchPath(path);
        }
    }

    // ---------------------------------------------------------------------
    // Assemblies
    // ---------------------------------------------------------------------

    [UnmanagedCallersOnly]
    public static unsafe IntPtr AssemblyLoad(IntPtr domainHandle, IntPtr pathUtf8, NativeExceptionInfo* exInfo)
    {
        try
        {
            var alc = Target<ClrppLoadContext>(domainHandle);
            var path = ReadUtf8(pathUtf8);

            if (alc == null)
            {
                throw new ArgumentException("Invalid domain handle");
            }

            if (string.IsNullOrEmpty(path) || !File.Exists(path))
            {
                throw new FileNotFoundException($"Assembly not found: {path}", path);
            }

            // Never duplicate the bridge assembly into a child context (same
            // rule as ClrppLoadContext.OnResolving): a second copy would have
            // its own uninitialized Bridge/InternalCalls state and distinct
            // type identities.
            var requestedName = System.Reflection.AssemblyName.GetAssemblyName(path);
            if (requestedName.Name == typeof(Bridge).Assembly.GetName().Name)
            {
                return Intern(typeof(Bridge).Assembly);
            }

            var assembly = alc.LoadFromPath(path);

            // Make the assembly's own directory a dependency search path.
            var dir = Path.GetDirectoryName(Path.GetFullPath(path));
            if (!string.IsNullOrEmpty(dir))
            {
                alc.AddSearchPath(dir);
            }

            return Intern(assembly);
        }
        catch (Exception ex)
        {
            FillException(ex, ref *exInfo);
            return IntPtr.Zero;
        }
    }

    [UnmanagedCallersOnly]
    public static IntPtr AssemblyGetCorlib()
    {
        return Intern(typeof(object).Assembly);
    }

    [UnmanagedCallersOnly]
    public static IntPtr AssemblyGetName(IntPtr assemblyHandle)
    {
        var assembly = Target<Assembly>(assemblyHandle);
        return AllocUtf8(assembly?.GetName().Name ?? string.Empty);
    }

    /// Returns utf8 with referenced assembly full names joined by '\n'.
    [UnmanagedCallersOnly]
    public static IntPtr AssemblyDumpReferences(IntPtr assemblyHandle)
    {
        var assembly = Target<Assembly>(assemblyHandle);
        if (assembly == null)
        {
            return IntPtr.Zero;
        }

        var refs = assembly.GetReferencedAssemblies().Select(r => r.FullName);
        return AllocUtf8(string.Join('\n', refs));
    }

    [UnmanagedCallersOnly]
    public static IntPtr AssemblyGetType(IntPtr assemblyHandle, IntPtr nameUtf8)
    {
        try
        {
            var assembly = Target<Assembly>(assemblyHandle);
            var name = ReadUtf8(nameUtf8);
            if (assembly == null || string.IsNullOrEmpty(name))
            {
                return IntPtr.Zero;
            }

            var type = FindType(assembly, name);
            return type != null ? Intern(type) : IntPtr.Zero;
        }
        catch (Exception ex)
        {
            Log($"AssemblyGetType failed: {ex}", "error");
            return IntPtr.Zero;
        }
    }

    internal static Type FindType(Assembly assembly, string fullOrSimpleName)
    {
        // Try the name verbatim first.
        var type = assembly.GetType(fullOrSimpleName, throwOnError: false);
        if (type != null)
        {
            return type;
        }

        // Handle nested type names expressed with '.' (mono style):
        // Tests.Nested.TestClassNested1.TestClassNested2 -> Tests.Nested.TestClassNested1+TestClassNested2
        var dotted = fullOrSimpleName;
        var lastDot = dotted.LastIndexOf('.');
        while (lastDot > 0)
        {
            dotted = dotted.Substring(0, lastDot) + "+" + dotted.Substring(lastDot + 1);
            type = assembly.GetType(dotted, throwOnError: false);
            if (type != null)
            {
                return type;
            }
            lastDot = dotted.LastIndexOf('.', lastDot - 1);
        }

        // Fall back to a simple name search across all exported/defined types.
        foreach (var candidate in SafeGetTypes(assembly))
        {
            if (candidate.Name == fullOrSimpleName)
            {
                return candidate;
            }
        }

        return null;
    }

    internal static IEnumerable<Type> SafeGetTypes(Assembly assembly)
    {
        try
        {
            return assembly.GetTypes();
        }
        catch (ReflectionTypeLoadException ex)
        {
            return ex.Types.Where(t => t != null);
        }
    }

    /// Fills the provided buffer with interned type handles. Returns the
    /// total count (call once with count=0 to size the buffer).
    [UnmanagedCallersOnly]
    public static unsafe int AssemblyGetTypes(IntPtr assemblyHandle, IntPtr* buffer, int capacity)
    {
        var assembly = Target<Assembly>(assemblyHandle);
        if (assembly == null)
        {
            return 0;
        }

        var types = SafeGetTypes(assembly).ToArray();
        if (buffer != null)
        {
            var count = Math.Min(capacity, types.Length);
            for (int i = 0; i < count; i++)
            {
                buffer[i] = Intern(types[i]);
            }
        }

        return types.Length;
    }

    [UnmanagedCallersOnly]
    public static unsafe int AssemblyGetTypesDerivedFrom(IntPtr assemblyHandle, IntPtr baseTypeHandle,
                                                         IntPtr* buffer, int capacity)
    {
        var assembly = Target<Assembly>(assemblyHandle);
        var baseType = Target<Type>(baseTypeHandle);
        if (assembly == null || baseType == null)
        {
            return 0;
        }

        var types = SafeGetTypes(assembly)
            .Where(t => t != baseType && baseType.IsAssignableFrom(t))
            .ToArray();

        if (buffer != null)
        {
            var count = Math.Min(capacity, types.Length);
            for (int i = 0; i < count; i++)
            {
                buffer[i] = Intern(types[i]);
            }
        }

        return types.Length;
    }
}

} // namespace Clrpp
