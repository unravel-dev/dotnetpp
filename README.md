![windows](https://github.com/unravel-dev/monopp/actions/workflows/windows.yml/badge.svg)
![linux](https://github.com/unravel-dev/monopp/actions/workflows/linux.yml/badge.svg)
![macos](https://github.com/unravel-dev/monopp/actions/workflows/macos.yml/badge.svg)

### A C++14 wrapper for embeddig C# into C++ using Mono. Clean and easy API.
### Only dependency is c++ standard library and Mono.
### Make sure you download the latest Mono stable version.
### You can download Mono from : http://www.mono-project.com/download/stable/
### Not feature complete but useable.

---
Consists of several small projects.
- monopp - C++14 wrapper for mono runtime, including a header-only POD converter layer (mono_managed.h) for binding custom pod types.
- clrpp - C++14 wrapper for the CoreCLR runtime (hostfxr + managed bridge), mirroring the monopp API.
- dotnetpp - unified dotnet:: API selecting monopp or clrpp at compile time (see dotnetpp/README.md).
- tests - tests and examples using the library.

---
C++
```c++
	if(!mono::init("mono", true))
	{
		return 1;
	}
	/// Create an app domain. When destructed it will
	/// unload all loaded assemblies
	mono::mono_domain my_domain("my_domain");

	/// Sets the current domain
	mono::mono_domain::set_current_domain(my_domain);

	/// Get or load an assembly
	auto assembly = my_domain.get_assembly("tests_managed.dll");

	/// Get access to a c# type
	auto type = assembly.get_type("Tests", "MonoppTest");

	/// Prints out Tests
	std::cout << type.get_namespace() << std::endl;

	/// Prints out MonoppTest
	std::cout << type.get_name() << std::endl;

	/// Prints out Tests.MonoppTest
	std::cout << type.get_fullname() << std::endl;

	/// Checks and gets the base type of Tests.MonoppTest
	if(type.has_base_type())
	{
		auto base_type = type.get_base_type();
		std::cout << base_type.get_fullname() << std::endl;
	}

	/// Create an instance of it. Default constructed.
	auto obj = type.new_instance();

	/// There are several ways of getting access to methods
	/// Way 1, name + arg count
	auto method1 = type.get_method("Method1", 0);

	/// You can invoke it by creating a thunk and calling it passing
	/// the object it belongs to as the first parameter. Not passing
	/// an object as the first parameter will treat it as a static method
	auto thunk1 = mono::make_method_invoker<void()>(method1);
	thunk1(obj);

	/// Way 2, name + args
	auto method2 = type.get_method("Method2(string)");
	auto thunk2 = mono::make_method_invoker<void(std::string)>(method2);
	thunk2(obj, "str_param");

	/// Way 3, use the template method
	auto method3 = mono::make_method_invoker<std::string(std::string, int)>(type, "Method5");
	auto result3 = method3(obj, "test", 5);

	/// You can also get and invoke static methods without passing
	/// an object as the first parameter
	auto method4 = mono::make_method_invoker<int(int)>(type, "Function1");
	auto result4 = method4(55);
	std::cout << result4 << std::endl;
	/// You can query various information about a method
	method1.get_name();
	method1.get_fullname();
	method1.get_full_declname();
	method1.get_visibility();
	method1.is_static();
	method1.get_param_types();
	method1.get_return_type();
	/// etc.

	/// You can catch exceptions like so
	try
	{
		mono::make_method_invoker<int(int, float)>(type, "NonExistingFunction");
	}
	catch(const mono::mono_exception& e)
	{
		std::cout << e.what() << std::endl;
	}

	/// You can access type fields by name
	auto field = type.get_field("someField");

	/// In order to get or set values to a field you need
	/// to create an invoker.
	auto mutable_field = mono::make_field_invoker<int>(field);

	/// You can get their values. Not passing an
	/// object as the parameter would treat
	/// it as being static.

	auto field_value = mutable_field.get_value(obj);
	std::cout << field_value << std::endl;

	/// You can set their values. Not passing an
	/// object as the parameter would treat
	/// it as being static.
	int field_arg = 55;
	mutable_field.set_value(obj, field_arg);

	/// You can query various information for a field.
	field.get_name();
	field.get_fullname();
	field.get_full_declname();
	field.get_type();
	field.get_visibility();
	field.is_static();
	/// etc..

	auto prop = type.get_property("someProperty");

	/// In order to get or set values to a field you need
	/// to create an invoker.
	auto mutable_prop = mono::make_property_invoker<int>(prop);

	/// You can get their values. Not passing an
	/// object as the parameter would treat
	/// it as being static.
	auto prop_value = mutable_prop.get_value(obj);
	std::cout << prop_value << std::endl;

	/// You can set their values. Not passing an
	/// object as the parameter would treat
	/// it as being static.
	int prop_arg = 55;
	mutable_prop.set_value(obj, prop_arg);

	/// You can get access to the get and set
	/// methods of a property
	auto getter = prop.get_get_method();
	auto setter = prop.get_set_method();
	/// You can treat these methods as you would any other method
	auto getter_thunk = mono::make_method_invoker<int()>(getter);
	prop_value = getter_thunk(obj);
	std::cout << prop_value << std::endl;

	auto setter_thunk = mono::make_method_invoker<void(int)>(setter);
	setter_thunk(obj, 12);

	/// You can query various information for a field.
	prop.get_name();
	prop.get_fullname();
	prop.get_full_declname();
	prop.get_type();
	prop.get_visibility();
	prop.is_static();
	/// etc..

	/// Get all the fields of the type
	auto fields = type.get_fields();

	for(const auto& f : fields)
	{
		std::cout << f.get_full_declname() << std::endl;
	}

	/// Get all the properties of the type
	auto props = type.get_properties();

	for(const auto& p : props)
	{
		std::cout << p.get_full_declname() << std::endl;
	}

	/// Get All the methods of the type
	auto methods = type.get_methods();

	for(const auto& method : methods)
	{
		std::cout << method.get_full_declname() << std::endl;
	}
	mono::shutdown();
```

---
C#
```c#
using System;
using System.Runtime.CompilerServices;

namespace Tests
{
class MonoppTest
{

	public int someField = 12;

	public int someProperty
	{
		get
		{
			return someField;
		}
		set
		{
			Console.WriteLine("FROM C# : Setting property value to {0}", value);
			someField = value;
		}
	}

	public static int someFieldStatic = 12;

	public static int somePropertyStatic
	{
		get
		{
			return someFieldStatic;
		}
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
		Console.WriteLine("FROM C# : Hello Mono World from instance.");
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
