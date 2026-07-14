#include "dotnetpp_suite.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

#include <dotnetpp/dotnetpp.h>
#include <dotnetpp/dotnet_managed.h>
#include <suitepp/suite.hpp>

/*
 * A single managed fixture serves both backends: mono resolves
 * [MethodImpl(InternalCall)] natively, coreclr weaves the same externs as
 * part of dotnet::compile (no Clrpp.* references needed in the fixture).
 */
#define DOTNETPP_TESTS_FIXTURE "managed/tests.cs"

namespace
{
struct dn_vec2
{
	float x;
	float y;
};

// Mirrors Tests.BoolPack: bool is 1 byte and char16_t 2 bytes on both
// sides, so the layouts agree byte for byte (bool@0, float@4, bool@8,
// char@10, size 12). Guards against interop-style marshalling creeping
// back in (4-byte BOOL would shift/corrupt every field after `before`).
struct dn_bool_pack
{
	bool before;
	float value;
	bool after;
	char16_t letter;
};

// Mirrors Tests.TwoBools (2 bytes - interop marshalling would claim 8).
struct dn_two_bools
{
	bool a;
	bool b;
};
} // namespace

namespace dotnetpp
{

void MyObject_CreateInternal(const dotnet::object& this_ptr, float x, const std::string& v)
{
	dotnet::ignore(this_ptr, x, v);
}

void MyObject_DestroyInternal(const dotnet::object& this_ptr)
{
	dotnet::ignore(this_ptr);
}

void MyObject_DoStuff(const dotnet::object& this_ptr, const std::string& value)
{
	dotnet::ignore(this_ptr, value);
}

auto MyObject_ReturnAString(const dotnet::object& this_ptr, const std::string& value) -> std::string
{
	dotnet::ignore(this_ptr, value);
	return "The value: " + value;
}

void MyVec_TestInternalPODCall(const dotnet::object& this_ptr, const dn_vec2& value)
{
	dotnet::ignore(this_ptr, value);
}

void MonoppTest_ThrowNative()
{
	dotnet::raise_exception("System", "InvalidOperationException", "native says no");
}

auto MonoppTest_NativeAdd(int a, int b) -> int
{
	return a + b;
}

// Bool-bearing struct by value in both directions.
auto PackTest_NativeInvertPack(const dn_bool_pack& pack) -> dn_bool_pack
{
	return {!pack.before, -pack.value, !pack.after, static_cast<char16_t>(pack.letter + 1)};
}

// Bool-bearing struct written through a by-ref (out) parameter.
void PackTest_NativeFillPack(dn_bool_pack* pack)
{
	pack->before = true;
	pack->value = 7.0f;
	pack->after = false;
	pack->letter = u'q';
}

// The engine's Entity pattern: managed passes a struct wrapping a single
// scalar (Tests.WrappedId), native receives the scalar itself - exactly
// like glue functions taking entt::entity. Only works with the by-value
// struct ABI (a one-field struct and its scalar are ABI-identical).
auto PackTest_NativeBumpId(uint32_t id) -> uint32_t
{
	return id + 1;
}

// Standalone utf16 char round-trip (widened to int32 on the clr wire).
auto PackTest_NativeNextChar(char16_t value) -> char16_t
{
	return static_cast<char16_t>(value + 1);
}

void test_suite()
{
	dotnet::domain domain("dotnetpp_domain");
	dotnet::domain::set_current_domain(domain);

	TEST_CASE("dotnetpp : load invalid assembly")
	{
		EXPECT_THROWS_AS(domain.get_assembly("doesnt_exist_12345.dll"), dotnet::exception);
	};

	TEST_CASE("dotnetpp : compile assembly")
	{
		dotnet::compiler_params cmd;
		cmd.files = {DATA_DIR DOTNETPP_TESTS_FIXTURE};
		cmd.output_name = DATA_DIR "dotnetpp_tests_managed.dll";

		bool compile_result = dotnet::compile(cmd);

		EXPECT(compile_result == true);
	};

	TEST_CASE("dotnetpp : load valid assembly")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto refs = assembly.dump_references();
			for(const auto& ref : refs)
			{
				std::cout << ref << std::endl;
			}
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : bind internal calls")
	{
		auto expression = [&]()
		{
			dotnet::add_internal_call("Tests.MyObject::CreateInternal",
									  dotnet_internal_call(MyObject_CreateInternal));
			dotnet::add_internal_call("Tests.MyObject::DestroyInternal",
									  dotnet_internal_call(MyObject_DestroyInternal));
			dotnet::add_internal_call("Tests.MyObject::DoStuff", dotnet_internal_call(MyObject_DoStuff));
			dotnet::add_internal_call("Tests.MyObject::ReturnAString",
									  dotnet_internal_call(MyObject_ReturnAString));

			dotnet::add_internal_call("Tests.MonortTest::TestInternalPODCall(Tests.Vector2f)",
									  dotnet_internal_call(MyVec_TestInternalPODCall));

			// Registry helper: same registration through a per-type prefix.
			dotnet::internal_call_registry registry("Tests.MonoppTest");
			registry.add_internal_call("ThrowNative", dotnet_internal_call(MonoppTest_ThrowNative));
			registry.add_internal_call("NativeAdd", dotnet_internal_call(MonoppTest_NativeAdd));

			dotnet::internal_call_registry pack_registry("Tests.PackTest");
			pack_registry.add_internal_call("NativeInvertPack",
											dotnet_internal_call(PackTest_NativeInvertPack));
			pack_registry.add_internal_call("NativeFillPack",
											dotnet_internal_call(PackTest_NativeFillPack));
			pack_registry.add_internal_call("NativeBumpId",
											dotnet_internal_call(PackTest_NativeBumpId));
			pack_registry.add_internal_call("NativeNextChar",
											dotnet_internal_call(PackTest_NativeNextChar));
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : get invalid type")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");

			auto type = assembly.get_type("SometypeThatDoesntExist12345");
			EXPECT(!type.valid());
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : get valid type")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");
			EXPECT(type.valid());
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : get nested valid type")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests.Nested", "TestClassNested1");
			EXPECT(type.valid());

			auto type2 = assembly.get_type("Tests.Nested.TestClassNested1");
			EXPECT(type2.valid());

			auto type3 = assembly.get_type("Tests.Nested.TestClassNested1.TestClassNested2");
			EXPECT(type3.valid());
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : type reflection")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");

			EXPECT(type.get_namespace() == std::string("Tests"));
			EXPECT(type.get_name() == std::string("MonoppTest"));
			EXPECT(type.get_fullname() == std::string("Tests.MonoppTest"));
			EXPECT(type.is_class());
			EXPECT(!type.is_struct());
			EXPECT(!type.is_enum());

			EXPECT(!type.get_fields().empty());
			EXPECT(!type.get_properties().empty());
			EXPECT(!type.get_methods().empty());
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : struct reflection")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "Vector2f");

			EXPECT(type.valid());
			EXPECT(type.is_struct());
			EXPECT(type.is_valuetype());
			EXPECT(!type.is_enum());
			EXPECT(type.get_sizeof() == sizeof(dn_vec2));
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : enum reflection")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "TestEnum");

			EXPECT(type.valid());
			EXPECT(type.is_enum());

			auto values = type.get_enum_values<int>();
			EXPECT(values.size() == 3);

			auto contains = [&](int value, const std::string& name)
			{
				return std::find_if(values.begin(), values.end(),
									[&](const std::pair<int, std::string>& entry) {
										return entry.first == value && entry.second == name;
									}) != values.end();
			};
			EXPECT(contains(1, "First"));
			EXPECT(contains(5, "Second"));
			EXPECT(contains(42, "Third"));
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : inheritance reflection")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto base = assembly.get_type("Tests", "MonoppTest");
			auto derived = assembly.get_type("Tests", "DerivedTest");

			EXPECT(derived.valid());
			EXPECT(derived.has_base_type());
			EXPECT(derived.get_base_type().get_fullname() == std::string("Tests.MonoppTest"));
			EXPECT(derived.is_derived_from(base));
			EXPECT(!base.is_derived_from(derived));

			auto derived_types = assembly.get_types_derived_from(base);
			bool found = std::find_if(derived_types.begin(), derived_types.end(),
									  [](const dotnet::type& t) {
										  return t.get_fullname() == "Tests.DerivedTest";
									  }) != derived_types.end();
			EXPECT(found);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : get valid method")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");

			auto method1 = dotnet::make_method_invoker<void()>(type, "Method1");
			auto method2 = dotnet::make_method_invoker<void(std::string)>(type, "Method2");
			auto method3 = dotnet::make_method_invoker<void(int)>(type, "Method3");
			auto method4 = dotnet::make_method_invoker<void(int, int)>(type, "Method4");
			auto method5 = dotnet::make_method_invoker<std::string(std::string, int)>(type, "Method5");

			dotnet::ignore(method1, method2, method3, method4, method5);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : get invalid method")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");
			EXPECT_THROWS_AS(dotnet::make_method_invoker<int(int, float)>(type, "NonExistingFunction"),
							 dotnet::exception);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : method metadata")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");

			auto method = type.get_method("Method5", 2);
			EXPECT(method.get_name() == std::string("Method5"));
			EXPECT(!method.is_static());
			EXPECT(method.get_visibility() == dotnet::visibility::vis_public);
			EXPECT(method.get_param_types().size() == 2);
			EXPECT(method.get_return_type().get_fullname() == std::string("System.String"));

			// Signature based lookup.
			auto method_sig = type.get_method("Method2(string)");
			EXPECT(method_sig.get_name() == std::string("Method2"));

			auto function = type.get_method("Function1", 1);
			EXPECT(function.is_static());
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : get/set field")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");

			auto field = type.get_field("someField");
			auto mutable_field = dotnet::make_field_invoker<int>(field);

			auto obj = type.new_instance();

			auto some_field = mutable_field.get_value(obj);
			EXPECT(some_field == 12);

			int arg = 6;
			mutable_field.set_value(obj, arg);

			some_field = mutable_field.get_value(obj);
			EXPECT(some_field == 6);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : get/set static field")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");

			auto field = type.get_field("someFieldStatic");
			auto mutable_field = dotnet::make_field_invoker<int>(field);

			auto some_field = mutable_field.get_value();
			EXPECT(some_field == 12);

			int arg = 6;
			mutable_field.set_value(arg);

			some_field = mutable_field.get_value();
			EXPECT(some_field == 6);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : get invalid field")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");
			EXPECT_THROWS(type.get_field("someInvalidField"));
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : field metadata")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");

			auto field = type.get_field("someField");
			EXPECT(field.get_name() == std::string("someField"));
			EXPECT(!field.is_static());
			EXPECT(field.get_visibility() == dotnet::visibility::vis_public);
			EXPECT(field.get_type().get_fullname() == std::string("System.Int32"));

			auto static_field = type.get_field("someFieldStatic");
			EXPECT(static_field.is_static());
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : get/set string field")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");

			auto field = type.get_field("someFieldStr");
			auto mutable_field = dotnet::make_field_invoker<std::string>(field);

			auto obj = type.new_instance();
			EXPECT(mutable_field.get_value(obj) == std::string("InitialString"));

			mutable_field.set_value(obj, std::string("Changed"));
			EXPECT(mutable_field.get_value(obj) == std::string("Changed"));

			auto static_field = type.get_field("someFieldStrStatic");
			auto mutable_static_field = dotnet::make_field_invoker<std::string>(static_field);
			EXPECT(mutable_static_field.get_value() == std::string("InitialStatic"));

			mutable_static_field.set_value(std::string("ChangedStatic"));
			EXPECT(mutable_static_field.get_value() == std::string("ChangedStatic"));
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : get/set field helpers")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");
			auto obj = type.new_instance();

			int value = 0;
			EXPECT(dotnet::get_field_value(obj, "someField", value));
			EXPECT(value == 12);

			EXPECT(dotnet::set_field_value(obj, "someField", 42));
			EXPECT(dotnet::get_field_value(obj, "someField", value));
			EXPECT(value == 42);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : get/set property")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");

			auto prop = type.get_property("someProperty");
			auto mutable_prop = dotnet::make_property_invoker<int>(prop);

			auto obj = type.new_instance();
			EXPECT(obj.valid());

			auto some_prop = mutable_prop.get_value(obj);
			EXPECT(some_prop == 12);

			int arg = 55;
			mutable_prop.set_value(obj, arg);

			some_prop = mutable_prop.get_value(obj);
			EXPECT(some_prop == 55);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : get/set static property")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");

			auto prop = type.get_property("somePropertyStatic");
			auto mutable_prop = dotnet::make_property_invoker<int>(prop);

			auto some_prop = mutable_prop.get_value();
			EXPECT(some_prop == 6);

			int arg = 55;
			mutable_prop.set_value(arg);

			some_prop = mutable_prop.get_value();
			EXPECT(some_prop == 55);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : get invalid property")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");
			EXPECT_THROWS(type.get_property("someInvalidProperty"));
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : property metadata and accessor methods")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");

			auto prop = type.get_property("someProperty");
			EXPECT(prop.get_name() == std::string("someProperty"));
			EXPECT(!prop.is_static());
			EXPECT(prop.get_visibility() == dotnet::visibility::vis_public);
			EXPECT(prop.get_type().get_fullname() == std::string("System.Int32"));

			// The get/set accessors behave as regular methods.
			auto obj = type.new_instance();
			auto getter_thunk = dotnet::make_method_invoker<int()>(prop.get_get_method());
			auto setter_thunk = dotnet::make_method_invoker<void(int)>(prop.get_set_method());

			setter_thunk(obj, 99);
			EXPECT(getter_thunk(obj) == 99);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : get/set string property")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");

			auto prop = type.get_property("somePropertyStr");
			auto mutable_prop = dotnet::make_property_invoker<std::string>(prop);

			auto obj = type.new_instance();
			EXPECT(mutable_prop.get_value(obj) == std::string("InitialString"));

			mutable_prop.set_value(obj, std::string("PropChanged"));
			EXPECT(mutable_prop.get_value(obj) == std::string("PropChanged"));
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : call static method 1")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");
			auto method_thunk = dotnet::make_method_invoker<int(int)>(type, "Function1");
			const auto number = 1000;
			auto result = method_thunk(number);
			EXPECT(number + 1337 == result);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : call static method 2")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");
			auto method_thunk = dotnet::make_method_invoker<void(float, int, float)>(type, "Function2");
			method_thunk(13.37f, 42, 9000.0f);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : call static method 3")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");
			auto method_thunk = dotnet::make_method_invoker<void(std::string)>(type, "Function3");
			method_thunk("Hello!");
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : call static method 4")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");
			auto method_thunk = dotnet::make_method_invoker<std::string(std::string)>(type, "Function4");
			auto expected_string = std::string("Hello!");
			auto result = method_thunk(expected_string);
			EXPECT(result == std::string("The string value was: " + expected_string));
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : call static method 5 (managed exception)")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");
			auto method_thunk = dotnet::make_method_invoker<void()>(type, "Function5");
			EXPECT_THROWS_AS(method_thunk(), dotnet::thunk_exception);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : call static method 6 (internal calls)")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");
			auto method_thunk = dotnet::make_method_invoker<void()>(type, "Function6");
			method_thunk();
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : primitive marshalling")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");

			auto echo_bool = dotnet::make_method_invoker<bool(bool)>(type, "EchoBool");
			EXPECT(echo_bool(false) == true);
			EXPECT(echo_bool(true) == false);

			auto echo_long = dotnet::make_method_invoker<std::int64_t(std::int64_t)>(type, "EchoLong");
			EXPECT(echo_long(1234567890123LL) == 1234567890124LL);

			auto echo_double = dotnet::make_method_invoker<double(double)>(type, "EchoDouble");
			EXPECT(echo_double(21.5) == 43.0);

			auto echo_float = dotnet::make_method_invoker<float(float)>(type, "EchoFloat");
			EXPECT(echo_float(1.5f) == 2.0f);

			auto sum =
				dotnet::make_method_invoker<double(int, float, double, std::int64_t)>(type, "Sum");
			EXPECT(sum(1, 2.0f, 3.0, 4) == 10.0);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : strings (unicode roundtrip)")
	{
		auto expression = [&]()
		{
			const std::string utf8 = u8"\u0417\u0434\u0440\u0430\u0432\u0435\u0439, \u4E16\u754C!";

			dotnet::string str(domain, utf8);
			EXPECT(str.as_utf8() == utf8);
			EXPECT(!str.as_utf16().empty());

			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");
			auto echo = dotnet::make_method_invoker<std::string(std::string)>(type, "Function4");
			EXPECT(echo(utf8) == std::string("The string value was: " + utf8));
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : call member method 1")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");
			auto obj = type.new_instance();
			auto method_thunk = dotnet::make_method_invoker<void()>(type, "Method1");
			method_thunk(obj);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : call member method 2")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");
			auto obj = type.new_instance();
			auto method_thunk = dotnet::make_method_invoker<std::string(std::string, int)>(type, "Method5");
			auto result = method_thunk(obj, "test", 5);
			EXPECT(result == std::string("Return Value: test"));
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : call member method POD")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonortTest");
			auto obj = type.new_instance();

			auto method_thunk = dotnet::make_method_invoker<dn_vec2(dn_vec2)>(type, "MethodPodAR");
			dn_vec2 p;
			p.x = 12;
			p.y = 15;
			auto result = method_thunk(obj, p);
			EXPECT(result.x == 165.0f);
			EXPECT(result.y == 7.0f);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : call internal call with POD arg")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonortTest");
			auto obj = type.new_instance();

			auto method_thunk = dotnet::make_method_invoker<void(dn_vec2)>(type, "TestInternalPODCall");
			dn_vec2 p;
			p.x = 5;
			p.y = 12;
			method_thunk(obj, p);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : internal call verified from managed")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");
			auto method_thunk = dotnet::make_method_invoker<bool()>(type, "CheckNativeAdd");
			EXPECT(method_thunk() == true);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : native exception caught in managed")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");
			auto method_thunk = dotnet::make_method_invoker<bool()>(type, "CatchNativeException");
			EXPECT(method_thunk() == true);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : member POD field")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonortTest");
			auto field = type.get_field("someFieldPOD");
			auto obj = type.new_instance();
			EXPECT(obj.valid());

			auto mutable_field = dotnet::make_field_invoker<dn_vec2>(field);
			auto some_field = mutable_field.get_value(obj);
			EXPECT(some_field.x == 12.0f);
			EXPECT(some_field.y == 13.0f);

			dn_vec2 arg = {6.0f, 7.0f};
			mutable_field.set_value(obj, arg);

			some_field = mutable_field.get_value(obj);
			EXPECT(some_field.x == 6.0f);
			EXPECT(some_field.y == 7.0f);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : member POD property")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonortTest");
			auto prop = type.get_property("somePropertyPOD");
			auto mutable_prop = dotnet::make_property_invoker<dn_vec2>(prop);

			auto obj = type.new_instance();
			EXPECT(obj.valid());

			auto some_prop = mutable_prop.get_value(obj);
			EXPECT(some_prop.x == 12.0f);
			EXPECT(some_prop.y == 13.0f);

			dn_vec2 arg = {55.0f, 56.0f};
			mutable_prop.set_value(obj, arg);

			some_prop = mutable_prop.get_value(obj);
			EXPECT(some_prop.x == 55.0f);
			EXPECT(some_prop.y == 56.0f);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : static POD field")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonortTest");
			auto field = type.get_field("someFieldPODStatic");
			auto mutable_field = dotnet::make_field_invoker<dn_vec2>(field);
			auto some_field = mutable_field.get_value();
			EXPECT(some_field.x == 12.0f);
			EXPECT(some_field.y == 13.0f);

			dn_vec2 arg = {6.0f, 7.0f};
			mutable_field.set_value(arg);

			some_field = mutable_field.get_value();
			EXPECT(some_field.x == 6.0f);
			EXPECT(some_field.y == 7.0f);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : static POD property")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonortTest");
			auto prop = type.get_property("somePropertyPODStatic");
			auto mutable_prop = dotnet::make_property_invoker<dn_vec2>(prop);

			auto some_prop = mutable_prop.get_value();
			EXPECT(some_prop.x == 6.0f);
			EXPECT(some_prop.y == 7.0f);

			dn_vec2 arg = {55.0f, 56.0f};
			mutable_prop.set_value(arg);

			some_prop = mutable_prop.get_value();
			EXPECT(some_prop.x == 55.0f);
			EXPECT(some_prop.y == 56.0f);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : POD box/unbox")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto vec_type = assembly.get_type("Tests", "Vector2f");

			dn_vec2 value = {3.0f, 4.0f};
			auto boxed = dotnet::box_value(value, vec_type);
			EXPECT(boxed.valid());

			auto unboxed = dotnet::unbox_value<dn_vec2>(boxed);
			EXPECT(unboxed.x == 3.0f);
			EXPECT(unboxed.y == 4.0f);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : bool-bearing struct layout and box/unbox")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");

			// Sizes must reflect the copied (CLR) layout, not the interop-
			// marshalled one (which would report 12 -> 20 and 2 -> 8).
			auto pack_type = assembly.get_type("Tests", "BoolPack");
			EXPECT(pack_type.get_sizeof() == sizeof(dn_bool_pack));

			auto flags_type = assembly.get_type("Tests", "TwoBools");
			EXPECT(flags_type.get_sizeof() == sizeof(dn_two_bools));

			dn_bool_pack value = {true, 2.5f, false, u'x'};
			auto boxed = dotnet::box_value(value, pack_type);
			EXPECT(boxed.valid());

			auto unboxed = dotnet::unbox_value<dn_bool_pack>(boxed);
			EXPECT(unboxed.before == true);
			EXPECT(unboxed.value == 2.5f);
			EXPECT(unboxed.after == false);
			EXPECT(unboxed.letter == u'x');
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : bool-bearing struct method arguments and returns")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "PackTest");

			// bool/char arguments in, struct blob out.
			auto make =
				dotnet::make_method_invoker<dn_bool_pack(bool, float, bool, char16_t)>(type, "MakePack");
			auto pack = make(true, 3.5f, false, u'k');
			EXPECT(pack.before == true);
			EXPECT(pack.value == 3.5f);
			EXPECT(pack.after == false);
			EXPECT(pack.letter == u'k');

			// Struct blob in, verified field-by-field on the managed side.
			auto check = dotnet::make_method_invoker<bool(dn_bool_pack)>(type, "CheckPack");
			EXPECT(check(pack) == true);

			// Struct in both directions.
			auto invert = dotnet::make_method_invoker<dn_bool_pack(dn_bool_pack)>(type, "InvertPack");
			auto inverted = invert(pack);
			EXPECT(inverted.before == false);
			EXPECT(inverted.value == -3.5f);
			EXPECT(inverted.after == true);
			EXPECT(inverted.letter == u'l');
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : bool-bearing struct fields")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "PackTest");

			auto pack_field = dotnet::make_field_invoker<dn_bool_pack>(type.get_field("packField"));
			auto pack = pack_field.get_value();
			EXPECT(pack.before == true);
			EXPECT(pack.value == 2.5f);
			EXPECT(pack.after == false);
			EXPECT(pack.letter == u'x');

			dn_bool_pack changed = {false, -1.5f, true, u'z'};
			pack_field.set_value(changed);
			pack = pack_field.get_value();
			EXPECT(pack.before == false);
			EXPECT(pack.value == -1.5f);
			EXPECT(pack.after == true);
			EXPECT(pack.letter == u'z');

			// 2-byte struct: fits its blob only with the CLR layout.
			auto flags_field = dotnet::make_field_invoker<dn_two_bools>(type.get_field("flagsField"));
			auto flags = flags_field.get_value();
			EXPECT(flags.a == true);
			EXPECT(flags.b == false);

			dn_two_bools swapped = {false, true};
			flags_field.set_value(swapped);
			flags = flags_field.get_value();
			EXPECT(flags.a == false);
			EXPECT(flags.b == true);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : bool-bearing struct internal calls")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "PackTest");

			// By-value struct icall, checked from the managed side.
			auto check_invert = dotnet::make_method_invoker<bool()>(type, "CheckNativeInvertPack");
			EXPECT(check_invert() == true);

			// By-ref (out) struct icall.
			auto check_fill = dotnet::make_method_invoker<bool()>(type, "CheckNativeFillPack");
			EXPECT(check_fill() == true);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : scalar-wrapper struct internal calls")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "PackTest");

			// Managed struct wrapping one scalar <-> native plain uint32_t,
			// the engine's Entity/entt::entity mapping. Regression test for
			// the pointer-passing ABI, which broke this silently (native
			// read the pointer bits as the id -> "Entity is invalid").
			auto check_bump = dotnet::make_method_invoker<bool()>(type, "CheckNativeBumpId");
			EXPECT(check_bump() == true);

			// Standalone utf16 chars (would truncate under ANSI marshalling).
			auto check_char = dotnet::make_method_invoker<bool()>(type, "CheckNativeNextChar");
			EXPECT(check_char() == true);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : bool arrays")
	{
		auto expression = [&]()
		{
			// bool[] elements are 1 byte on both sides.
			std::vector<bool> values = {true, false, true, true};
			dotnet::array<bool> arr(values);
			EXPECT(arr.size() == values.size());
			for(size_t i = 0; i < values.size(); ++i)
			{
				EXPECT(arr.get(i) == values[i]);
			}

			arr.set(1, true);
			EXPECT(arr.get(1) == true);

			auto back = arr.to_vector();
			EXPECT(back.size() == values.size());
			EXPECT(back[1] == true);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : typed struct arrays")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");

			// A real managed element type (not the raw byte fallback):
			// bulk copies must handle non-primitive element arrays.
			auto vec_type = assembly.get_type("Tests", "Vector2f");
			std::vector<dn_vec2> vecs = {{1.0f, 2.0f}, {3.0f, 4.0f}, {5.0f, 6.0f}};
			dotnet::array<dn_vec2> vec_arr(vecs, vec_type);
			EXPECT(vec_arr.size() == vecs.size());

			auto element = vec_arr.get(1);
			EXPECT(element.x == 3.0f);
			EXPECT(element.y == 4.0f);

			vec_arr.set(0, {9.0f, 8.0f});
			element = vec_arr.get(0);
			EXPECT(element.x == 9.0f);
			EXPECT(element.y == 8.0f);

			auto back = vec_arr.to_vector();
			EXPECT(back.size() == vecs.size());
			EXPECT(back[2].y == 6.0f);

			// Element type with sub-word fields: per-element offsets only
			// line up when both sides agree on the 12-byte layout.
			auto pack_type = assembly.get_type("Tests", "BoolPack");
			std::vector<dn_bool_pack> packs = {{true, 1.0f, false, u'a'}, {false, 2.0f, true, u'b'}};
			dotnet::array<dn_bool_pack> pack_arr(packs, pack_type);
			EXPECT(pack_arr.size() == packs.size());

			auto pack = pack_arr.get(1);
			EXPECT(pack.before == false);
			EXPECT(pack.value == 2.0f);
			EXPECT(pack.after == true);
			EXPECT(pack.letter == u'b');
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : arrays")
	{
		auto expression = [&]()
		{
			std::vector<int> values = {1, 2, 3, 4, 5};
			dotnet::array<int> arr(values);
			EXPECT(arr.size() == values.size());
			for(size_t i = 0; i < values.size(); ++i)
			{
				EXPECT(arr.get(i) == values[i]);
			}

			arr.set(0, 42);
			EXPECT(arr.get(0) == 42);

			auto back = arr.to_vector();
			EXPECT(back.size() == values.size());
			EXPECT(back[0] == 42);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : arrays as method arguments")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");

			auto sum_array = dotnet::make_method_invoker<int(std::vector<int>)>(type, "SumArray");
			EXPECT(sum_array(std::vector<int>{1, 2, 3, 4}) == 10);

			std::vector<int> values = {10, 20, 30};
			dotnet::array<int> arr(values);
			auto sum_managed_array = dotnet::make_method_invoker<int(dotnet::array<int>)>(type, "SumArray");
			EXPECT(sum_managed_array(arr) == 60);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : lists")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");

			auto make_list = dotnet::make_method_invoker<dotnet::list<int>()>(type, "MakeList");
			auto list = make_list();
			EXPECT(list.valid());
			EXPECT(list.size() == 3);
			EXPECT(list.get(0) == 1);
			EXPECT(list.get(2) == 3);

			list.set(0, 42);
			list.add(7);
			EXPECT(list.size() == 4);
			EXPECT(list.get(0) == 42);
			EXPECT(list.get(3) == 7);

			auto sum_list = dotnet::make_method_invoker<int(dotnet::list<int>)>(type, "SumList");
			EXPECT(sum_list(list) == 42 + 2 + 3 + 7);

			list.remove_at(0);
			EXPECT(list.size() == 3);
			EXPECT(sum_list(list) == 2 + 3 + 7);

			// Native side list construction.
			std::vector<int> values = {5, 6, 7};
			dotnet::list<int> native_list(values, dotnet::type{});
			EXPECT(native_list.valid());
			EXPECT(sum_list(native_list) == 18);
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : gc handles")
	{
		auto expression = [&]()
		{
			auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_tests_managed.dll");
			auto type = assembly.get_type("Tests", "MonoppTest");
			auto obj = type.new_instance();

			dotnet::scoped_gc_handle handle(obj);
			EXPECT(handle.is_locked());
			EXPECT(handle.get_object().valid());

			auto pinned = dotnet::make_object_pinned(obj);
			EXPECT(pinned->is_locked());

			dotnet::gc_collect();
			EXPECT(handle.get_object().valid());
		};
		EXPECT_NOTHROWS(expression());
	};

	TEST_CASE("dotnetpp : gc stats")
	{
		auto expression = [&]()
		{
			auto heap = dotnet::gc_get_heap_size();
			auto used = dotnet::gc_get_used_size();
			EXPECT(heap >= 0);
			EXPECT(used >= 0);
		};
		EXPECT_NOTHROWS(expression());
	};
}

} // namespace dotnetpp
