using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Clrpp
{

public static partial class Bridge
{
    private const BindingFlags AllInstanceStatic =
        BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static;

    private const BindingFlags AllDeclared = AllInstanceStatic | BindingFlags.DeclaredOnly;

    // ---------------------------------------------------------------------
    // Type queries
    // ---------------------------------------------------------------------

    [UnmanagedCallersOnly]
    public static IntPtr TypeGetName(IntPtr typeHandle)
    {
        var type = Target<Type>(typeHandle);
        return AllocUtf8(type?.Name ?? string.Empty);
    }

    [UnmanagedCallersOnly]
    public static IntPtr TypeGetNamespace(IntPtr typeHandle)
    {
        var type = Target<Type>(typeHandle);
        return AllocUtf8(type?.Namespace ?? string.Empty);
    }

    /// Mono style fullname: nested types separated with '.', generic args in [] notation kept simple.
    [UnmanagedCallersOnly]
    public static IntPtr TypeGetFullname(IntPtr typeHandle)
    {
        var type = Target<Type>(typeHandle);
        if (type == null)
        {
            return AllocUtf8(string.Empty);
        }

        return AllocUtf8(GetMonoStyleFullName(type));
    }

    internal static string GetMonoStyleFullName(Type type)
    {
        if (type.IsGenericType)
        {
            var definition = type.GetGenericTypeDefinition();
            var name = (definition.FullName ?? definition.Name).Replace('+', '.');
            var tick = name.IndexOf('`');
            if (tick > 0)
            {
                name = name.Substring(0, tick);
            }

            var args = type.GetGenericArguments().Select(GetMonoStyleFullName);
            return $"{name}<{string.Join(",", args)}>";
        }

        var full = type.FullName ?? type.Name;
        return full.Replace('+', '.');
    }

    [UnmanagedCallersOnly]
    public static int TypeGetFlags(IntPtr typeHandle)
    {
        var type = Target<Type>(typeHandle);
        if (type == null)
        {
            return 0;
        }

        int flags = 0;
        if (type.IsValueType) flags |= 1 << 0;
        if (type.IsEnum) flags |= 1 << 1;
        if (type.IsClass) flags |= 1 << 2;
        if (type.IsAbstract) flags |= 1 << 3;
        if (type.IsSealed) flags |= 1 << 4;
        if (type.IsInterface) flags |= 1 << 5;
#pragma warning disable SYSLIB0050 // mirrors mono_class serializable flag
        if (type.IsSerializable) flags |= 1 << 6;
#pragma warning restore SYSLIB0050
        if (type == typeof(string)) flags |= 1 << 7;
        if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(List<>)) flags |= 1 << 8;
        if (type.IsArray) flags |= 1 << 9;
        return flags;
    }

    [UnmanagedCallersOnly]
    public static IntPtr TypeGetBaseType(IntPtr typeHandle)
    {
        var type = Target<Type>(typeHandle);
        var baseType = type?.BaseType;
        return baseType != null ? Intern(baseType) : IntPtr.Zero;
    }

    [UnmanagedCallersOnly]
    public static IntPtr TypeGetNestingType(IntPtr typeHandle)
    {
        var type = Target<Type>(typeHandle);
        var declaring = type?.DeclaringType;
        return declaring != null ? Intern(declaring) : IntPtr.Zero;
    }

    [UnmanagedCallersOnly]
    public static unsafe int TypeGetNestedTypes(IntPtr typeHandle, IntPtr* buffer, int capacity)
    {
        var type = Target<Type>(typeHandle);
        if (type == null)
        {
            return 0;
        }

        var result = new List<Type>();
        GatherNested(type, result);

        if (buffer != null)
        {
            var count = Math.Min(capacity, result.Count);
            for (int i = 0; i < count; i++)
            {
                buffer[i] = Intern(result[i]);
            }
        }

        return result.Count;

        static void GatherNested(Type t, List<Type> into)
        {
            foreach (var nested in t.GetNestedTypes(BindingFlags.Public | BindingFlags.NonPublic))
            {
                into.Add(nested);
                GatherNested(nested, into);
            }
        }
    }

    [UnmanagedCallersOnly]
    public static int TypeIsDerivedFrom(IntPtr typeHandle, IntPtr baseHandle)
    {
        var type = Target<Type>(typeHandle);
        var baseType = Target<Type>(baseHandle);
        if (type == null || baseType == null)
        {
            return 0;
        }

        return baseType.IsAssignableFrom(type) ? 1 : 0;
    }

    [UnmanagedCallersOnly]
    public static IntPtr TypeGetEnumBaseType(IntPtr typeHandle)
    {
        var type = Target<Type>(typeHandle);
        if (type == null || !type.IsEnum)
        {
            return IntPtr.Zero;
        }

        return Intern(Enum.GetUnderlyingType(type));
    }

    /// Returns count of enum entries; fills values (as long) and interned utf8 names.
    [UnmanagedCallersOnly]
    public static unsafe int TypeGetEnumValues(IntPtr typeHandle, long* values, IntPtr* names, int capacity)
    {
        var type = Target<Type>(typeHandle);
        if (type == null || !type.IsEnum)
        {
            return 0;
        }

        var enumNames = Enum.GetNames(type);
        var enumValues = Enum.GetValues(type);

        if (values != null && names != null)
        {
            var count = Math.Min(capacity, enumNames.Length);
            for (int i = 0; i < count; i++)
            {
                values[i] = Convert.ToInt64(enumValues.GetValue(i));
                names[i] = AllocUtf8(enumNames[i]);
            }
        }

        return enumNames.Length;
    }

    [UnmanagedCallersOnly]
    public static int TypeGetRank(IntPtr typeHandle)
    {
        var type = Target<Type>(typeHandle);
        if (type == null)
        {
            return 0;
        }

        return type.IsArray ? type.GetArrayRank() : 0;
    }

    /// Element type of arrays or List&lt;T&gt;.
    [UnmanagedCallersOnly]
    public static IntPtr TypeGetElementType(IntPtr typeHandle)
    {
        var type = Target<Type>(typeHandle);
        if (type == null)
        {
            return IntPtr.Zero;
        }

        if (type.IsArray)
        {
            return Intern(type.GetElementType());
        }

        if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(List<>))
        {
            return Intern(type.GetGenericArguments()[0]);
        }

        return IntPtr.Zero;
    }

    /// Size as copied across the boundary: the CLR layout for value types
    /// (bool = 1, char = 2 - NOT Marshal.SizeOf, which reports the interop
    /// layout), pointer size for reference types. 0 for types that cannot
    /// cross by value (reference-bearing structs).
    [UnmanagedCallersOnly]
    public static int TypeGetSizeof(IntPtr typeHandle)
    {
        var type = Target<Type>(typeHandle);
        if (type == null)
        {
            return 0;
        }

        try
        {
            return type.IsValueType ? ClrLayout.SizeOf(type) : IntPtr.Size;
        }
        catch
        {
            return 0;
        }
    }

    [UnmanagedCallersOnly]
    public static int TypeGetAlignof(IntPtr typeHandle)
    {
        var type = Target<Type>(typeHandle);
        if (type == null)
        {
            return 0;
        }

        try
        {
            return ClrAlignOf(type);
        }
        catch
        {
            return IntPtr.Size;
        }
    }

    /// Approximation matching C++ rules for blittable data: primitives align
    /// to their own size (capped at pointer size), structs to their most
    /// aligned field, recursively.
    private static int ClrAlignOf(Type type)
    {
        if (!type.IsValueType)
        {
            return IntPtr.Size;
        }

        if (type.IsEnum)
        {
            return ClrLayout.SizeOf(Enum.GetUnderlyingType(type));
        }

        if (type.IsPrimitive)
        {
            return Math.Min(ClrLayout.SizeOf(type), IntPtr.Size);
        }

        int align = 1;
        foreach (var field in type.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic))
        {
            align = Math.Max(align, ClrAlignOf(field.FieldType));
        }

        return align;
    }

    [UnmanagedCallersOnly]
    public static IntPtr TypeGetListType(IntPtr elementTypeHandle)
    {
        var elementType = Target<Type>(elementTypeHandle);
        if (elementType == null)
        {
            return IntPtr.Zero;
        }

        try
        {
            return Intern(typeof(List<>).MakeGenericType(elementType));
        }
        catch (Exception ex)
        {
            Log($"TypeGetListType failed for {elementType}: {ex.Message}", "error");
            return IntPtr.Zero;
        }
    }

    // ---------------------------------------------------------------------
    // Members
    // ---------------------------------------------------------------------

    [UnmanagedCallersOnly]
    public static IntPtr TypeGetMethod(IntPtr typeHandle, IntPtr nameUtf8, int argc)
    {
        var type = Target<Type>(typeHandle);
        var name = ReadUtf8(nameUtf8);
        if (type == null || string.IsNullOrEmpty(name))
        {
            return IntPtr.Zero;
        }

        for (var t = type; t != null; t = t.BaseType)
        {
            MethodBase chosen = null;
            var chosenIsPublic = false;
            var chosenToken = int.MaxValue;

            foreach (var method in t.GetMethods(AllDeclared))
            {
                if (method.Name != name)
                {
                    continue;
                }

                if (argc >= 0 && method.GetParameters().Length != argc)
                {
                    continue;
                }

                if (IsBetterMethodCandidate(method, chosen, chosenIsPublic, chosenToken))
                {
                    chosen = method;
                    chosenIsPublic = method.IsPublic;
                    chosenToken = method.MetadataToken;
                }
            }

            if (chosen != null)
            {
                return Intern(chosen);
            }

            // Constructors are exposed as ".ctor" like in mono.
            if (name == ".ctor")
            {
                chosen = null;
                chosenIsPublic = false;
                chosenToken = int.MaxValue;
                foreach (var ctor in t.GetConstructors(AllDeclared))
                {
                    if (argc >= 0 && ctor.GetParameters().Length != argc)
                    {
                        continue;
                    }

                    if (IsBetterMethodCandidate(ctor, chosen, chosenIsPublic, chosenToken))
                    {
                        chosen = ctor;
                        chosenIsPublic = ctor.IsPublic;
                        chosenToken = ctor.MetadataToken;
                    }
                }

                if (chosen != null)
                {
                    return Intern(chosen);
                }
            }
        }

        return IntPtr.Zero;
    }

    /// Prefer public overloads; break remaining ties with MetadataToken for
    /// stable binding when multiple same-arity overloads exist. Callers that
    /// need a specific overload should use TypeGetMethodBySignature.
    private static bool IsBetterMethodCandidate(MethodBase candidate, MethodBase current,
                                                bool currentIsPublic, int currentToken)
    {
        if (current == null)
        {
            return true;
        }

        if (candidate.IsPublic != currentIsPublic)
        {
            return candidate.IsPublic;
        }

        return candidate.MetadataToken < currentToken;
    }

    /// Signature format matches mono_method_desc: "Name(typename,typename)".
    /// Type names may be simple ("int", "string"), full ("System.Int32") or
    /// user type full names ("Tests.Vector2f").
    [UnmanagedCallersOnly]
    public static IntPtr TypeGetMethodBySignature(IntPtr typeHandle, IntPtr signatureUtf8)
    {
        var type = Target<Type>(typeHandle);
        var signature = ReadUtf8(signatureUtf8);
        if (type == null || string.IsNullOrEmpty(signature))
        {
            return IntPtr.Zero;
        }

        var open = signature.IndexOf('(');
        var close = signature.LastIndexOf(')');
        string name;
        string[] argNames;
        if (open < 0 || close < open)
        {
            name = signature;
            argNames = null; // any arity
        }
        else
        {
            name = signature.Substring(0, open);
            var argsPart = signature.Substring(open + 1, close - open - 1).Trim();
            argNames = argsPart.Length == 0
                ? Array.Empty<string>()
                : argsPart.Split(',').Select(a => a.Trim()).ToArray();
        }

        for (var t = type; t != null; t = t.BaseType)
        {
            IEnumerable<MethodBase> members = t.GetMethods(AllDeclared).Where(m => m.Name == name);
            if (name == ".ctor")
            {
                members = t.GetConstructors(AllDeclared);
            }

            foreach (var member in members)
            {
                if (argNames == null)
                {
                    return Intern(member);
                }

                var parameters = member.GetParameters();
                if (parameters.Length != argNames.Length)
                {
                    continue;
                }

                bool match = true;
                for (int i = 0; i < parameters.Length; i++)
                {
                    if (!TypeNameMatches(parameters[i].ParameterType, argNames[i]))
                    {
                        match = false;
                        break;
                    }
                }

                if (match)
                {
                    return Intern(member);
                }
            }
        }

        return IntPtr.Zero;
    }

    private static readonly Dictionary<string, Type> Shortcuts = new()
    {
        ["char"] = typeof(char),
        ["bool"] = typeof(bool),
        ["byte"] = typeof(byte),
        ["sbyte"] = typeof(sbyte),
        ["uint16"] = typeof(ushort),
        ["ushort"] = typeof(ushort),
        ["int16"] = typeof(short),
        ["short"] = typeof(short),
        ["uint"] = typeof(uint),
        ["int"] = typeof(int),
        ["ulong"] = typeof(ulong),
        ["long"] = typeof(long),
        ["uintptr"] = typeof(UIntPtr),
        ["intptr"] = typeof(IntPtr),
        ["single"] = typeof(float),
        ["float"] = typeof(float),
        ["double"] = typeof(double),
        ["string"] = typeof(string),
        ["object"] = typeof(object),
        ["void"] = typeof(void),
    };

    internal static bool TypeNameMatches(Type type, string name)
    {
        if (Shortcuts.TryGetValue(name, out var shortcut))
        {
            return type == shortcut;
        }

        var full = GetMonoStyleFullName(type);
        return full == name || type.Name == name;
    }

    [UnmanagedCallersOnly]
    public static unsafe int TypeGetMethods(IntPtr typeHandle, int includeBase, IntPtr* buffer, int capacity)
    {
        var type = Target<Type>(typeHandle);
        if (type == null)
        {
            return 0;
        }

        var flags = includeBase != 0 ? AllInstanceStatic : AllDeclared;
        var methods = type.GetMethods(flags);

        if (buffer != null)
        {
            var count = Math.Min(capacity, methods.Length);
            for (int i = 0; i < count; i++)
            {
                buffer[i] = Intern(methods[i]);
            }
        }

        return methods.Length;
    }

    [UnmanagedCallersOnly]
    public static IntPtr TypeGetField(IntPtr typeHandle, IntPtr nameUtf8)
    {
        var type = Target<Type>(typeHandle);
        var name = ReadUtf8(nameUtf8);
        if (type == null || string.IsNullOrEmpty(name))
        {
            return IntPtr.Zero;
        }

        for (var t = type; t != null; t = t.BaseType)
        {
            var field = t.GetField(name, AllDeclared);
            if (field != null)
            {
                return Intern(field);
            }
        }

        return IntPtr.Zero;
    }

    [UnmanagedCallersOnly]
    public static unsafe int TypeGetFields(IntPtr typeHandle, int includeBase, IntPtr* buffer, int capacity)
    {
        var type = Target<Type>(typeHandle);
        if (type == null)
        {
            return 0;
        }

        var flags = includeBase != 0 ? AllInstanceStatic : AllDeclared;
        var fields = type.GetFields(flags);

        if (buffer != null)
        {
            var count = Math.Min(capacity, fields.Length);
            for (int i = 0; i < count; i++)
            {
                buffer[i] = Intern(fields[i]);
            }
        }

        return fields.Length;
    }

    [UnmanagedCallersOnly]
    public static IntPtr TypeGetProperty(IntPtr typeHandle, IntPtr nameUtf8)
    {
        var type = Target<Type>(typeHandle);
        var name = ReadUtf8(nameUtf8);
        if (type == null || string.IsNullOrEmpty(name))
        {
            return IntPtr.Zero;
        }

        for (var t = type; t != null; t = t.BaseType)
        {
            var property = t.GetProperty(name, AllDeclared);
            if (property != null)
            {
                return Intern(property);
            }
        }

        return IntPtr.Zero;
    }

    [UnmanagedCallersOnly]
    public static unsafe int TypeGetProperties(IntPtr typeHandle, int includeBase, IntPtr* buffer, int capacity)
    {
        var type = Target<Type>(typeHandle);
        if (type == null)
        {
            return 0;
        }

        var flags = includeBase != 0 ? AllInstanceStatic : AllDeclared;
        var properties = type.GetProperties(flags);

        if (buffer != null)
        {
            var count = Math.Min(capacity, properties.Length);
            for (int i = 0; i < count; i++)
            {
                buffer[i] = Intern(properties[i]);
            }
        }

        return properties.Length;
    }

    /// Attribute instantiation runs user attribute constructors and resolves
    /// attribute types - both can throw, which must not escape an
    /// UnmanagedCallersOnly export (process fail-fast).
    private static object[] SafeGetCustomAttributes(MemberInfo member, bool inherit)
    {
        try
        {
            return member.GetCustomAttributes(inherit);
        }
        catch (Exception ex)
        {
            Log($"GetCustomAttributes failed for {member}: {ex.Message}", "error");
            return Array.Empty<object>();
        }
    }

    /// Attribute instances on a type (as new object handles).
    [UnmanagedCallersOnly]
    public static unsafe int TypeGetAttributes(IntPtr typeHandle, int includeBase, IntPtr* buffer, int capacity)
    {
        var type = Target<Type>(typeHandle);
        if (type == null)
        {
            return 0;
        }

        var attrs = SafeGetCustomAttributes(type, includeBase != 0);
        if (buffer != null)
        {
            var count = Math.Min(capacity, attrs.Length);
            for (int i = 0; i < count; i++)
            {
                buffer[i] = NewObjectHandle(attrs[i]);
            }
        }

        return attrs.Length;
    }

    // ---------------------------------------------------------------------
    // Method info
    // ---------------------------------------------------------------------

    [UnmanagedCallersOnly]
    public static IntPtr MethodGetName(IntPtr methodHandle)
    {
        var method = Target<MethodBase>(methodHandle);
        return AllocUtf8(method?.Name ?? string.Empty);
    }

    [UnmanagedCallersOnly]
    public static IntPtr MethodGetFullname(IntPtr methodHandle)
    {
        var method = Target<MethodBase>(methodHandle);
        if (method == null)
        {
            return AllocUtf8(string.Empty);
        }

        var declaring = method.DeclaringType != null ? GetMonoStyleFullName(method.DeclaringType) : "?";
        var parameters = string.Join(",", method.GetParameters().Select(p => GetMonoStyleFullName(p.ParameterType)));
        return AllocUtf8($"{declaring}:{method.Name} ({parameters})");
    }

    [UnmanagedCallersOnly]
    public static int MethodGetFlags(IntPtr methodHandle)
    {
        var method = Target<MethodBase>(methodHandle);
        if (method == null)
        {
            return 0;
        }

        int flags = 0;
        if (method.IsStatic) flags |= 1 << 0;
        if (method.IsVirtual) flags |= 1 << 1;
        if ((method.Attributes & MethodAttributes.PinvokeImpl) != 0) flags |= 1 << 2;
        if (method.IsSpecialName) flags |= 1 << 3;
        if ((method.GetMethodImplementationFlags() & MethodImplAttributes.InternalCall) != 0) flags |= 1 << 4;
        if ((method.GetMethodImplementationFlags() & MethodImplAttributes.Synchronized) != 0) flags |= 1 << 5;

        // visibility in bits 8..10
        int vis;
        if (method.IsPublic) vis = 4;
        else if (method.IsFamily) vis = 3;
        else if (method.IsAssembly) vis = 2;
        else if (method.IsFamilyAndAssembly) vis = 1;
        else vis = 0;
        flags |= vis << 8;

        return flags;
    }

    [UnmanagedCallersOnly]
    public static IntPtr MethodGetReturnType(IntPtr methodHandle)
    {
        var method = Target<MethodBase>(methodHandle);
        if (method is MethodInfo mi)
        {
            return Intern(mi.ReturnType);
        }

        if (method is ConstructorInfo ci)
        {
            return Intern(typeof(void));
        }

        return IntPtr.Zero;
    }

    [UnmanagedCallersOnly]
    public static unsafe int MethodGetParamTypes(IntPtr methodHandle, IntPtr* buffer, int capacity)
    {
        var method = Target<MethodBase>(methodHandle);
        if (method == null)
        {
            return 0;
        }

        var parameters = method.GetParameters();
        if (buffer != null)
        {
            var count = Math.Min(capacity, parameters.Length);
            for (int i = 0; i < count; i++)
            {
                buffer[i] = Intern(parameters[i].ParameterType);
            }
        }

        return parameters.Length;
    }

    [UnmanagedCallersOnly]
    public static unsafe int MethodGetAttributes(IntPtr methodHandle, IntPtr* buffer, int capacity)
    {
        var method = Target<MethodBase>(methodHandle);
        if (method == null)
        {
            return 0;
        }

        var attrs = SafeGetCustomAttributes(method, false).Select(a => a.GetType()).ToArray();
        if (buffer != null)
        {
            var count = Math.Min(capacity, attrs.Length);
            for (int i = 0; i < count; i++)
            {
                buffer[i] = Intern(attrs[i]);
            }
        }

        return attrs.Length;
    }

    [UnmanagedCallersOnly]
    public static IntPtr MethodGetDeclaringType(IntPtr methodHandle)
    {
        var method = Target<MemberInfo>(methodHandle);
        var declaring = method?.DeclaringType;
        return declaring != null ? Intern(declaring) : IntPtr.Zero;
    }

    // ---------------------------------------------------------------------
    // Field info
    // ---------------------------------------------------------------------

    [UnmanagedCallersOnly]
    public static IntPtr FieldGetName(IntPtr fieldHandle)
    {
        var field = Target<FieldInfo>(fieldHandle);
        return AllocUtf8(field?.Name ?? string.Empty);
    }

    [UnmanagedCallersOnly]
    public static IntPtr FieldGetType(IntPtr fieldHandle)
    {
        var field = Target<FieldInfo>(fieldHandle);
        return field != null ? Intern(field.FieldType) : IntPtr.Zero;
    }

    [UnmanagedCallersOnly]
    public static int FieldGetFlags(IntPtr fieldHandle)
    {
        var field = Target<FieldInfo>(fieldHandle);
        if (field == null)
        {
            return 0;
        }

        int flags = 0;
        if (field.IsStatic) flags |= 1 << 0;
        if (field.IsInitOnly) flags |= 1 << 1;
        if (field.IsLiteral) flags |= 1 << 2;
        if (field.Name.Contains("k__BackingField")) flags |= 1 << 3;

        int vis;
        if (field.IsPublic) vis = 4;
        else if (field.IsFamily) vis = 3;
        else if (field.IsAssembly) vis = 2;
        else if (field.IsFamilyAndAssembly) vis = 1;
        else vis = 0;
        flags |= vis << 8;

        return flags;
    }

    [UnmanagedCallersOnly]
    public static IntPtr FieldGetDeclaringType(IntPtr fieldHandle)
    {
        var field = Target<FieldInfo>(fieldHandle);
        var declaring = field?.DeclaringType;
        return declaring != null ? Intern(declaring) : IntPtr.Zero;
    }

    /// Attribute instances on a field (new object handles).
    [UnmanagedCallersOnly]
    public static unsafe int FieldGetAttributes(IntPtr fieldHandle, IntPtr* buffer, int capacity)
    {
        var field = Target<FieldInfo>(fieldHandle);
        if (field == null)
        {
            return 0;
        }

        var attrs = SafeGetCustomAttributes(field, false);
        if (buffer != null)
        {
            var count = Math.Min(capacity, attrs.Length);
            for (int i = 0; i < count; i++)
            {
                buffer[i] = NewObjectHandle(attrs[i]);
            }
        }

        return attrs.Length;
    }

    // ---------------------------------------------------------------------
    // Property info
    // ---------------------------------------------------------------------

    [UnmanagedCallersOnly]
    public static IntPtr PropertyGetName(IntPtr propertyHandle)
    {
        var property = Target<PropertyInfo>(propertyHandle);
        return AllocUtf8(property?.Name ?? string.Empty);
    }

    [UnmanagedCallersOnly]
    public static IntPtr PropertyGetType(IntPtr propertyHandle)
    {
        var property = Target<PropertyInfo>(propertyHandle);
        return property != null ? Intern(property.PropertyType) : IntPtr.Zero;
    }

    [UnmanagedCallersOnly]
    public static IntPtr PropertyGetGetMethod(IntPtr propertyHandle)
    {
        var property = Target<PropertyInfo>(propertyHandle);
        var method = property?.GetGetMethod(nonPublic: true);
        return method != null ? Intern(method) : IntPtr.Zero;
    }

    [UnmanagedCallersOnly]
    public static IntPtr PropertyGetSetMethod(IntPtr propertyHandle)
    {
        var property = Target<PropertyInfo>(propertyHandle);
        var method = property?.GetSetMethod(nonPublic: true);
        return method != null ? Intern(method) : IntPtr.Zero;
    }

    [UnmanagedCallersOnly]
    public static IntPtr PropertyGetDeclaringType(IntPtr propertyHandle)
    {
        var property = Target<PropertyInfo>(propertyHandle);
        var declaring = property?.DeclaringType;
        return declaring != null ? Intern(declaring) : IntPtr.Zero;
    }

    [UnmanagedCallersOnly]
    public static int PropertyGetFlags(IntPtr propertyHandle)
    {
        var property = Target<PropertyInfo>(propertyHandle);
        if (property == null)
        {
            return 0;
        }

        var accessor = property.GetGetMethod(true) ?? property.GetSetMethod(true);

        int flags = 0;
        if (accessor != null && accessor.IsStatic) flags |= 1 << 0;
        if (property.CanRead && !property.CanWrite) flags |= 1 << 1; // readonly
        if (accessor != null && accessor.IsSpecialName) flags |= 1 << 3;
        if (property.GetCustomAttribute<System.ComponentModel.DefaultValueAttribute>() != null) flags |= 1 << 4;

        int vis = 0;
        if (accessor != null)
        {
            if (accessor.IsPublic) vis = 4;
            else if (accessor.IsFamily) vis = 3;
            else if (accessor.IsAssembly) vis = 2;
            else if (accessor.IsFamilyAndAssembly) vis = 1;
        }
        flags |= vis << 8;

        return flags;
    }

    /// Attribute instances on a property (new object handles).
    [UnmanagedCallersOnly]
    public static unsafe int PropertyGetAttributes(IntPtr propertyHandle, IntPtr* buffer, int capacity)
    {
        var property = Target<PropertyInfo>(propertyHandle);
        if (property == null)
        {
            return 0;
        }

        var attrs = SafeGetCustomAttributes(property, false);
        if (buffer != null)
        {
            var count = Math.Min(capacity, attrs.Length);
            for (int i = 0; i < count; i++)
            {
                buffer[i] = NewObjectHandle(attrs[i]);
            }
        }

        return attrs.Length;
    }
}

} // namespace Clrpp
