![windows](https://github.com/unravel-dev/monopp/actions/workflows/windows.yml/badge.svg)
![linux](https://github.com/unravel-dev/monopp/actions/workflows/linux.yml/badge.svg)
![macos](https://github.com/unravel-dev/monopp/actions/workflows/macos.yml/badge.svg)

# dotnetpp

A C++14 library for embedding C# in native apps. You write against a single
`dotnet::` API; at compile time it maps onto one of two runtimes:

| Backend | CMake value | Under the hood |
| ------- | ----------- | -------------- |
| **CoreCLR** (default) | `coreclr` | `clrpp` — `hostfxr` + managed bridge (`Clrpp.Managed.dll`) |
| **Mono** | `mono` | `monopp` — classic Mono embedding |

Domains, assemblies, types, method/field/property invokers, arrays, lists,
GC handles, internal calls, the compiler driver, and the POD converter layer
look the same from C++. Managed-side differences (especially internal calls
on CoreCLR) are documented in [`dotnetpp/README.md`](dotnetpp/README.md).

---

## Layout

- **`dotnetpp/`** — unified `dotnet::` headers (what you include)
- **`clrpp/`** — CoreCLR backend
- **`monopp/`** — Mono backend
- **`tests/`** — shared suite (`dotnetpp_suite.cpp`) that runs on either backend

---

## Requirements

**CoreCLR (default)**

- A recent [.NET SDK](https://dotnet.microsoft.com/download) on `PATH` (`dotnet`)
- The managed bridge (`Clrpp.Managed.dll`) next to your executable (or under
  the configured `managed_dir`)

**Mono**

- A recent [Mono](https://www.mono-project.com/download/stable/) install
  (`mcs` / mono runtime libraries)

Only the C++ standard library is required on the native side beyond the
chosen runtime.

---

## Build

```bash
cmake -S . -B build -DDOTNETPP_BACKEND=coreclr   # or mono
cmake --build build
```

Useful options:

| Option | Default | Meaning |
| ------ | ------- | ------- |
| `DOTNETPP_BACKEND` | `coreclr` | `mono` or `coreclr` |
| `BUILD_DOTNETPP_TESTS` | ON when top-level | Build the unified test suite |
| `BUILD_DOTNETPP_SHARED` | ON | Shared vs static |

```cpp
#include <dotnetpp/dotnetpp.h>   // umbrella
// or individual headers, e.g. <dotnetpp/dotnet_jit.h>
```

---

## Quick start (C++)

```cpp
#include <iostream>
#include <dotnetpp/dotnetpp.h>

int main()
{
	// CoreCLR: discovers the SDK / bridge. Mono: finds mcs / runtime.
	dotnet::compiler_paths paths;
	if(!dotnet::init(paths))
	{
		return 1;
	}

	// Optional (CoreCLR): force the experimental interpreter for testing.
	// Pre-existing DOTNET_* env vars are never overridden.
	//
	//   dotnet::interpreter_config interp;
	//   interp.interp_mode = dotnet::interpreter_config::mode::forced;
	//   dotnet::init(paths, {}, interp);

	/// Domain owns loaded assemblies; destroying it unloads them
	/// (AppDomain on Mono, collectible ALC on CoreCLR).
	dotnet::domain my_domain("my_domain");
	dotnet::domain::set_current_domain(my_domain);

	auto assembly = my_domain.get_assembly("tests_managed.dll");
	auto type = assembly.get_type("Tests", "MonoppTest");

	std::cout << type.get_namespace() << '\n'; // Tests
	std::cout << type.get_name() << '\n';      // MonoppTest
	std::cout << type.get_fullname() << '\n';     // Tests.MonoppTest

	if(type.has_base_type())
	{
		std::cout << type.get_base_type().get_fullname() << '\n';
	}

	auto obj = type.new_instance();

	/// Method by name + arity
	auto method1 = type.get_method("Method1", 0);
	auto thunk1 = dotnet::make_method_invoker<void()>(method1);
	thunk1(obj); // omit `obj` for static methods

	/// Method by signature string
	auto method2 = type.get_method("Method2(string)");
	auto thunk2 = dotnet::make_method_invoker<void(std::string)>(method2);
	thunk2(obj, "str_param");

	/// Convenience: resolve + invoker in one step
	auto method3 = dotnet::make_method_invoker<std::string(std::string, int)>(type, "Method5");
	auto result3 = method3(obj, "test", 5);

	auto method4 = dotnet::make_method_invoker<int(int)>(type, "Function1");
	std::cout << method4(55) << '\n';

	try
	{
		dotnet::make_method_invoker<int(int, float)>(type, "NonExistingFunction");
	}
	catch(const dotnet::exception& e)
	{
		std::cout << e.what() << '\n';
	}

	/// Fields
	auto field = type.get_field("someField");
	auto mutable_field = dotnet::make_field_invoker<int>(field);
	std::cout << mutable_field.get_value(obj) << '\n';
	mutable_field.set_value(obj, 55);

	/// Properties
	auto prop = type.get_property("someProperty");
	auto mutable_prop = dotnet::make_property_invoker<int>(prop);
	std::cout << mutable_prop.get_value(obj) << '\n';
	mutable_prop.set_value(obj, 55);

	auto getter = prop.get_get_method();
	auto setter = prop.get_set_method();
	auto getter_thunk = dotnet::make_method_invoker<int()>(getter);
	auto setter_thunk = dotnet::make_method_invoker<void(int)>(setter);
	std::cout << getter_thunk(obj) << '\n';
	setter_thunk(obj, 12);

	for(const auto& f : type.get_fields())
		std::cout << f.get_full_declname() << '\n';
	for(const auto& p : type.get_properties())
		std::cout << p.get_full_declname() << '\n';
	for(const auto& m : type.get_methods())
		std::cout << m.get_full_declname() << '\n';

	dotnet::shutdown();
	return 0;
}
```

POD / custom types use the shared converter protocol (`to_managed` /
`from_managed`). For layout-compatible pairs:

```cpp
dotnet_register_converter_for_pod(my_vec2, managed_vec2);
```

See [`dotnetpp/README.md`](dotnetpp/README.md) for converters, CoreCLR
`[InternalCall]` weaving, domains/ALCs, and the compiler driver.

---

## Matching C# fixture

```csharp
using System;
using System.Runtime.CompilerServices;

namespace Tests
{
class MonoppTest
{
	public int someField = 12;

	public int someProperty
	{
		get { return someField; }
		set
		{
			Console.WriteLine("FROM C# : Setting property value to {0}", value);
			someField = value;
		}
	}

	public static int someFieldStatic = 12;

	public static int somePropertyStatic
	{
		get { return someFieldStatic; }
		set
		{
			Console.WriteLine("FROM C# : Setting static property value to {0}", value);
			someFieldStatic = value;
		}
	}

	static MonoppTest()
	{
		Console.WriteLine("FROM C# : STATIC CONSTRUCTOR.");
	}

	public MonoppTest()
	{
		Console.WriteLine("FROM C# : MonoppTest created.");
	}

	~MonoppTest()
	{
		Console.WriteLine("FROM C# : MonoppTest destroyed.");
	}

	void Method1()
	{
		Console.WriteLine("FROM C# : Hello from instance.");
	}

	void Method2(string s)
	{
		Console.WriteLine("FROM C# : WithParam string: " + s);
	}

	void Method3(int s)
	{
		Console.WriteLine("FROM C# : WithParam int: " + s);
	}

	void Method4(int s, int s1)
	{
		Console.WriteLine("FROM C# : WithParam int, int: {0}, {1}", s, s1);
	}

	public string Method5(string s, int b)
	{
		Console.WriteLine("FROM C# : WithParam: {0}, {1}", s, b);
		return "Return Value: " + s;
	}

	public static int Function1(int a)
	{
		Console.WriteLine("FROM C# : Int value: " + a);
		return a + 1337;
	}

	public static void Function2(float a, int b, float c)
	{
		Console.WriteLine("FROM C# : VoidMethod: {0}, {1}, {2}", a, b, c);
	}

	public static void Function3(string a)
	{
		Console.WriteLine("FROM C# : String value: {0}", a);
	}

	public static string Function4(string str)
	{
		return "The string value was: " + str;
	}

	public static void Function5()
	{
		throw new Exception("Hello!");
	}
}
}
```

Internal calls stay Mono-style on both backends:

```csharp
[MethodImpl(MethodImplOptions.InternalCall)]
public extern string ReturnAString(string value);
```

On CoreCLR, `dotnet::compile` weaves those externs into real `calli` bodies.
Details are in [`dotnetpp/README.md`](dotnetpp/README.md).
