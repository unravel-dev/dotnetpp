/*
 * dotnetpp performance harness. Builds the same benchmark set on both
 * backends so mono and coreclr numbers can be compared directly: build one
 * tree with -DDOTNETPP_BACKEND=mono and one with -DDOTNETPP_BACKEND=coreclr,
 * run monopp_perf from each and diff the tables.
 *
 * Three benchmark groups:
 *   - managed:  pure C# loops entered through a single invoke, measuring
 *               runtime execution quality (JIT/interpreter, allocator, GC).
 *   - m2n:      managed loops that perform one internal call per iteration,
 *               measuring the managed -> native crossing.
 *   - n2m:      native loops that perform one reflection invoke (or
 *               field/property access) per iteration, measuring the
 *               native -> managed crossing.
 *   - array/list: bulk copies, element access, handle passing, and
 *               collection arguments with both blittable and converted types.
 *   - combo:    mixed patterns (vector arg + managed sum, per-element
 *               converted invoke over an array, static field storage).
 */

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <dotnetpp/dotnetpp.h>
#include <dotnetpp/dotnet_managed.h>

namespace
{

struct perf_vec2
{
	float x;
	float y;
};
} // namespace

/*
 * Custom converted type, mirroring the engine's math::color pattern: the
 * native representation (packed bytes) differs from the managed one (four
 * floats, Perf.Color), so every boundary crossing runs a real conversion
 * through the converter layer instead of a raw copy.
 */
struct perf_color
{
	std::uint8_t r;
	std::uint8_t g;
	std::uint8_t b;
	std::uint8_t a;
};

/// Layout mirror of Perf.Color - the type that actually crosses the wire.
struct perf_color_managed
{
	float r;
	float g;
	float b;
	float a;
};

// Conversion specializations must be visible before the registration macro
// instantiates them (same ordering the engine uses in script_interop.h).
namespace dotnetpp_backend
{
namespace managed_interface
{

namespace
{
auto to_byte(float value) -> std::uint8_t
{
	const float scaled = value * 255.0f;
	const float clamped = scaled < 0.0f ? 0.0f : (scaled > 255.0f ? 255.0f : scaled);
	return static_cast<std::uint8_t>(clamped);
}
} // namespace

template <>
auto converter::convert(const perf_color& c) -> perf_color_managed
{
	return {static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f,
			static_cast<float>(c.b) / 255.0f, static_cast<float>(c.a) / 255.0f};
}

template <>
auto converter::convert(const perf_color_managed& c) -> perf_color
{
	return {to_byte(c.r), to_byte(c.g), to_byte(c.b), to_byte(c.a)};
}

} // namespace managed_interface
} // namespace dotnetpp_backend

dotnet_register_converter_for_pod(perf_color, perf_color_managed);

namespace
{

auto PerfTest_NativeAdd(int a, int b) -> int
{
	return a + b;
}

auto PerfTest_NativeVecLenSq(const perf_vec2& v) -> float
{
	return v.x * v.x + v.y * v.y;
}

auto PerfTest_NativeEcho(const std::string& s) -> std::string
{
	return s;
}

auto PerfTest_NativeBrighten(const perf_color& c) -> perf_color
{
	perf_color result = c;
	result.r = static_cast<std::uint8_t>(result.r / 2 + 25);
	result.g = static_cast<std::uint8_t>(result.g / 2 + 25);
	result.b = static_cast<std::uint8_t>(result.b / 2 + 25);
	return result;
}

struct bench_result
{
	std::string name;
	std::int64_t iterations{};
	double total_ms{};
	double ns_per_op{};
};

std::vector<bench_result> results;

/*
 * Time fn(n): fn must perform exactly n operations (either a native loop or
 * a single managed call that loops internally - both patterns reduce to
 * "n ops behind one callable"). A smaller warmup run first gets one-time
 * costs (JIT of the touched paths, invoker caches) out of the measurement.
 */
template <typename F>
void bench(const std::string& name, std::int64_t iterations, F&& fn)
{
	const auto warmup = iterations / 100 > 0 ? iterations / 100 : 1;
	fn(warmup);

	const auto start = std::chrono::steady_clock::now();
	fn(iterations);
	const auto end = std::chrono::steady_clock::now();

	const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

	bench_result result;
	result.name = name;
	result.iterations = iterations;
	result.total_ms = static_cast<double>(ns) / 1e6;
	result.ns_per_op = static_cast<double>(ns) / static_cast<double>(iterations);
	results.push_back(result);

	std::printf("%-38s %12lld ops %10.2f ms %12.1f ns/op\n", name.c_str(),
				static_cast<long long>(iterations), result.total_ms, result.ns_per_op);
	std::fflush(stdout);
}

void print_summary(const char* backend)
{
	std::printf("\n===== dotnetpp perf summary (backend: %s) =====\n", backend);
	std::printf("%-38s %14s %13s %15s\n", "benchmark", "iterations", "total ms", "ns/op");
	for(const auto& r : results)
	{
		std::printf("%-38s %14lld %13.2f %15.1f\n", r.name.c_str(), static_cast<long long>(r.iterations),
					r.total_ms, r.ns_per_op);
	}
}

} // namespace

int main()
{
	dotnet::compiler_paths paths;
	if(!dotnet::init(paths))
	{
		return 1;
	}

	{
		dotnet::domain domain("dotnetpp_perf_domain");
		dotnet::domain::set_current_domain(domain);

		// Register icalls before the assembly loads so they pre-bind.
		dotnet::internal_call_registry registry("Perf.PerfTest");
		registry.add_internal_call("NativeAdd", dotnet_internal_call(PerfTest_NativeAdd));
		registry.add_internal_call("NativeVecLenSq", dotnet_internal_call(PerfTest_NativeVecLenSq));
		registry.add_internal_call("NativeEcho", dotnet_internal_call(PerfTest_NativeEcho));
		registry.add_internal_call("NativeBrighten", dotnet_internal_call(PerfTest_NativeBrighten));

		// Compile without debug info so the measurement reflects optimized IL.
		dotnet::compiler_params cmd;
		cmd.files = {DATA_DIR "managed/perf.cs"};
		cmd.output_name = DATA_DIR "dotnetpp_perf_managed.dll";
		cmd.debug = false;

		if(!dotnet::compile(cmd))
		{
			std::printf("perf: failed to compile managed fixture\n");
			return 1;
		}

		auto assembly = domain.get_assembly(DATA_DIR "dotnetpp_perf_managed.dll");
		auto type = assembly.get_type("Perf", "PerfTest");

		// ---- managed-only: one invoke, n iterations inside C# --------------
		{
			auto sum_loop = dotnet::make_method_invoker<std::int64_t(int)>(type, "SumLoop");
			bench("managed: int arithmetic loop", 20000000, [&](std::int64_t n) { sum_loop(static_cast<int>(n)); });

			auto struct_math = dotnet::make_method_invoker<float(int)>(type, "StructMathLoop");
			bench("managed: struct float math loop", 10000000,
				  [&](std::int64_t n) { struct_math(static_cast<int>(n)); });

			auto string_loop = dotnet::make_method_invoker<int(int)>(type, "StringLoop");
			bench("managed: string concat loop", 200000, [&](std::int64_t n) { string_loop(static_cast<int>(n)); });

			auto alloc_loop = dotnet::make_method_invoker<int(int)>(type, "AllocLoop");
			bench("managed: small alloc loop (gc)", 2000000,
				  [&](std::int64_t n) { alloc_loop(static_cast<int>(n)); });

			auto dict_loop = dotnet::make_method_invoker<int(int)>(type, "DictionaryLoop");
			bench("managed: dictionary add/lookup", 1000000,
				  [&](std::int64_t n) { dict_loop(static_cast<int>(n)); });

			auto array_fill = dotnet::make_method_invoker<std::int64_t(int)>(type, "ArrayFillLoop");
			bench("managed: array index loop", 20000000,
				  [&](std::int64_t n) { array_fill(static_cast<int>(n)); });

			auto list_churn = dotnet::make_method_invoker<int(int)>(type, "ListChurnLoop");
			bench("managed: list add/index/clear", 5000000,
				  [&](std::int64_t n) { list_churn(static_cast<int>(n)); });

			auto vec2_array = dotnet::make_method_invoker<std::int64_t(int)>(type, "Vec2ArrayLoop");
			bench("managed: Vec2[] index loop", 10000000,
				  [&](std::int64_t n) { vec2_array(static_cast<int>(n)); });

			auto vec2_list = dotnet::make_method_invoker<int(int)>(type, "Vec2ListLoop");
			bench("managed: List<Vec2> churn loop", 2000000,
				  [&](std::int64_t n) { vec2_list(static_cast<int>(n)); });

			auto color_math = dotnet::make_method_invoker<float(int)>(type, "ColorMathLoop");
			bench("managed: Color struct math loop", 5000000,
				  [&](std::int64_t n) { color_math(static_cast<int>(n)); });
		}

		// ---- managed -> native: icall per iteration ------------------------
		{
			auto icall_add = dotnet::make_method_invoker<std::int64_t(int)>(type, "IcallAddLoop");
			bench("m2n: icall int add", 5000000, [&](std::int64_t n) { icall_add(static_cast<int>(n)); });

			auto icall_struct = dotnet::make_method_invoker<float(int)>(type, "IcallStructLoop");
			bench("m2n: icall pod struct", 5000000, [&](std::int64_t n) { icall_struct(static_cast<int>(n)); });

			auto icall_string = dotnet::make_method_invoker<int(int)>(type, "IcallStringLoop");
			bench("m2n: icall string echo", 200000, [&](std::int64_t n) { icall_string(static_cast<int>(n)); });

			auto icall_color = dotnet::make_method_invoker<float(int)>(type, "IcallColorLoop");
			bench("m2n: icall converted custom type", 2000000,
				  [&](std::int64_t n) { icall_color(static_cast<int>(n)); });
		}

		// ---- native -> managed: invoke per iteration ------------------------
		{
			auto add = dotnet::make_method_invoker<int(int, int)>(type, "Add");
			bench("n2m: invoke static int add", 1000000,
				  [&](std::int64_t n)
				  {
					  int acc = 0;
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  acc += add(static_cast<int>(i), 1);
					  }
					  dotnet::ignore(acc);
				  });

			auto obj = type.new_instance();
			auto add_instance = dotnet::make_method_invoker<int(int, int)>(type, "AddInstance");
			bench("n2m: invoke instance int add", 1000000,
				  [&](std::int64_t n)
				  {
					  int acc = 0;
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  acc += add_instance(obj, static_cast<int>(i), 1);
					  }
					  dotnet::ignore(acc);
				  });

			auto echo_vec = dotnet::make_method_invoker<perf_vec2(perf_vec2)>(type, "EchoVec");
			bench("n2m: invoke pod struct echo", 500000,
				  [&](std::int64_t n)
				  {
					  perf_vec2 v{1.0f, 2.0f};
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  v = echo_vec(v);
					  }
					  dotnet::ignore(v);
				  });

			// Arity 4: Portable blittable binder caps at 3; Compiled path binds this.
			auto add4 = dotnet::make_method_invoker<int(int, int, int, int)>(type, "Add4");
			bench("n2m: invoke static int add4", 1000000,
				  [&](std::int64_t n)
				  {
					  int acc = 0;
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  acc += add4(static_cast<int>(i), 1, 2, 3);
					  }
					  dotnet::ignore(acc);
				  });

			auto echo_string = dotnet::make_method_invoker<std::string(std::string)>(type, "EchoString");
			bench("n2m: invoke string echo", 200000,
				  [&](std::int64_t n)
				  {
					  const std::string payload = "perf string payload";
					  std::size_t acc = 0;
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  acc += echo_string(payload).size();
					  }
					  dotnet::ignore(acc);
				  });

			auto field = type.get_field("counter");
			auto field_invoker = dotnet::make_field_invoker<int>(field);
			bench("n2m: field get+set int", 500000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  auto value = field_invoker.get_value(obj);
						  field_invoker.set_value(obj, value + 1);
					  }
				  });

			auto prop = type.get_property("counterProperty");
			auto prop_invoker = dotnet::make_property_invoker<int>(prop);
			bench("n2m: property get+set int", 500000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  auto value = prop_invoker.get_value(obj);
						  prop_invoker.set_value(obj, value + 1);
					  }
				  });

			// Converted custom type: float color managed-side, packed bytes
			// native-side, converter runs on argument and return.
			auto brighten = dotnet::make_method_invoker<perf_color(perf_color)>(type, "Brighten");
			bench("n2m: invoke converted custom type", 500000,
				  [&](std::int64_t n)
				  {
					  perf_color c{25, 50, 75, 255};
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  c = brighten(c);
					  }
					  dotnet::ignore(c);
				  });

			auto color_field = type.get_field("colorField");
			auto color_field_invoker = dotnet::make_field_invoker<perf_color>(color_field);
			bench("n2m: static Color field get+set", 500000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  auto c = color_field_invoker.get_value();
						  color_field_invoker.set_value(c);
					  }
				  });
		}

		auto vec_type = assembly.get_type("Perf", "Vec2");
		auto color_type = assembly.get_type("Perf", "Color");
		{
			std::vector<int> ints_1k(1024);
			for(std::size_t i = 0; i < ints_1k.size(); ++i)
			{
				ints_1k[i] = static_cast<int>(i);
			}

			// Whole-vector argument: a fresh managed array is created and
			// filled per call.
			auto sum_vector = dotnet::make_method_invoker<int(std::vector<int>)>(type, "SumArray");
			bench("array: vector<int>[1024] arg per call", 20000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  sum_vector(ints_1k);
					  }
				  });

			bench("array: create int[1024] from vector", 20000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  dotnet::array<int> arr(ints_1k);
						  dotnet::ignore(arr);
					  }
				  });

			dotnet::array<int> persistent_ints(ints_1k);
			bench("array: to_vector int[1024]", 20000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  auto back = persistent_ints.to_vector();
						  dotnet::ignore(back);
					  }
				  });

			// Existing managed array passed by handle - no bulk copy per call.
			auto sum_array = dotnet::make_method_invoker<int(dotnet::array<int>)>(type, "SumArray");
			bench("array: pass handle + managed sum", 20000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  sum_array(persistent_ints);
					  }
				  });

			bench("array: element get+set int", 200000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  auto index = static_cast<std::size_t>(i) & 1023u;
						  auto value = persistent_ints.get(index);
						  persistent_ints.set(index, value + 1);
					  }
				  });

			bench("array: bulk read via get x1024", 20000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t r = 0; r < n; ++r)
					  {
						  int acc = 0;
						  for(std::size_t i = 0; i < 1024u; ++i)
						  {
							  acc += persistent_ints.get(i);
						  }
						  dotnet::ignore(acc);
					  }
				  });

			auto make_array = dotnet::make_method_invoker<dotnet::object(int)>(type, "MakeArray");
			bench("array: managed MakeArray[1024]", 5000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  dotnet::array<int> arr(make_array(1024));
						  dotnet::ignore(arr);
					  }
				  });

			// Typed struct elements: per-element layout copies, not the raw
			// primitive fast path.
			std::vector<perf_vec2> vecs_256(256);
			for(std::size_t i = 0; i < vecs_256.size(); ++i)
			{
				vecs_256[i] = {static_cast<float>(i), static_cast<float>(i) * 2.0f};
			}
			bench("array: struct Vec2[256] roundtrip", 10000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  dotnet::array<perf_vec2> arr(vecs_256, vec_type);
						  auto back = arr.to_vector();
						  dotnet::ignore(back);
					  }
				  });

			auto sum_vec_array =
				dotnet::make_method_invoker<float(dotnet::array<perf_vec2>)>(type, "SumVecArray");
			dotnet::array<perf_vec2> persistent_vecs(vecs_256, vec_type);
			bench("array: pass Vec2[256] handle + sum", 20000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  sum_vec_array(persistent_vecs);
					  }
				  });

			// Converted custom element type: native packed bytes, managed floats.
			std::vector<perf_color> colors_256(256);
			for(std::size_t i = 0; i < colors_256.size(); ++i)
			{
				const auto byte = static_cast<std::uint8_t>(i & 255u);
				colors_256[i] = {byte, byte, byte, 255};
			}
			bench("array: Color[256] roundtrip (converter)", 10000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  dotnet::array<perf_color> arr(colors_256, color_type);
						  auto back = arr.to_vector();
						  dotnet::ignore(back);
					  }
				  });

			dotnet::array<perf_color> persistent_colors(colors_256, color_type);
			bench("array: Color element get+set (converter)", 100000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  auto index = static_cast<std::size_t>(i) & 255u;
						  auto value = persistent_colors.get(index);
						  persistent_colors.set(index, value);
					  }
				  });

			auto sum_color_array =
				dotnet::make_method_invoker<float(dotnet::array<perf_color>)>(type, "SumColorArray");
			bench("array: pass Color[256] handle + sum", 20000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  sum_color_array(persistent_colors);
					  }
				  });

			// Converted element types need an explicit managed element type when
			// building the array; a bare vector<perf_color> argument falls back
			// to System.Byte[] and fails at invoke time.
			bench("array: rebuild Color[256] arg per call", 10000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  dotnet::array<perf_color> arr(colors_256, color_type);
						  sum_color_array(arr);
					  }
				  });

			auto brighten_colors =
				dotnet::make_method_invoker<int(dotnet::array<perf_color>)>(type, "BrightenColors");
			bench("array: in-place Color[] brighten invoke", 10000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  brighten_colors(persistent_colors);
					  }
				  });
		}

		// ---- lists: List<T> creation, element ops, handle passing -----------
		{
			std::vector<int> ints_1k(1024);
			for(std::size_t i = 0; i < ints_1k.size(); ++i)
			{
				ints_1k[i] = static_cast<int>(i);
			}

			bench("list: create List<int>[1024]", 5000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  dotnet::list<int> list(ints_1k, dotnet::type{});
						  dotnet::ignore(list);
					  }
				  });

			dotnet::list<int> persistent_list(ints_1k, dotnet::type{});
			auto sum_list = dotnet::make_method_invoker<int(dotnet::list<int>)>(type, "SumList");
			bench("list: pass handle + managed sum", 20000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  sum_list(persistent_list);
					  }
				  });

			bench("list: element get+set int", 100000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  auto index = static_cast<std::size_t>(i) & 1023u;
						  auto value = persistent_list.get(index);
						  persistent_list.set(index, value + 1);
					  }
				  });

			auto make_list = dotnet::make_method_invoker<dotnet::object(int)>(type, "MakeList");
			bench("list: managed create + read back", 5000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  dotnet::list<int> list(make_list(1024));
						  // Avoid List<int>::to_vector() on mono: that helper fills
						  // null reference slots and only applies to object elements.
						  int acc = 0;
						  for(std::size_t j = 0; j < list.size(); ++j)
						  {
							  acc += list.get(j);
						  }
						  dotnet::ignore(acc);
					  }
				  });

			std::vector<int> empty;
			dotnet::list<int> growing_list(empty, dotnet::type{});
			bench("list: native Add() growth", 500000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  growing_list.add(static_cast<int>(i));
						  if(growing_list.size() >= 1024u)
						  {
							  growing_list.clear();
						  }
					  }
				  });

			std::vector<perf_vec2> vecs_256(256);
			for(std::size_t i = 0; i < vecs_256.size(); ++i)
			{
				vecs_256[i] = {static_cast<float>(i), static_cast<float>(i) * 2.0f};
			}
			dotnet::list<perf_vec2> persistent_vec_list(vecs_256, vec_type);
			auto sum_vec_list =
				dotnet::make_method_invoker<float(dotnet::list<perf_vec2>)>(type, "SumVecList");
			bench("list: pass List<Vec2>[256] handle + sum", 20000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  sum_vec_list(persistent_vec_list);
					  }
				  });

			auto make_vec_list = dotnet::make_method_invoker<dotnet::object(int)>(type, "MakeVecList");
			bench("list: managed MakeVecList[256]", 5000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  dotnet::list<perf_vec2> list(make_vec_list(256));
						  dotnet::ignore(list);
					  }
				  });

			std::vector<perf_color> colors_64(64);
			for(std::size_t i = 0; i < colors_64.size(); ++i)
			{
				const auto byte = static_cast<std::uint8_t>((i * 3u) & 255u);
				colors_64[i] = {byte, byte, byte, 255};
			}
			dotnet::list<perf_color> persistent_color_list(colors_64, color_type);
			bench("list: Color element get+set (converter)", 100000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  auto index = static_cast<std::size_t>(i) & 63u;
						  auto value = persistent_color_list.get(index);
						  persistent_color_list.set(index, value);
					  }
				  });

			auto sum_color_array =
				dotnet::make_method_invoker<float(dotnet::array<perf_color>)>(type, "SumColorArray");
			bench("list: Color[64] array arg per call", 10000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t i = 0; i < n; ++i)
					  {
						  dotnet::array<perf_color> arr(colors_64, color_type);
						  sum_color_array(arr);
					  }
				  });
		}

		// ---- combo: per-element converted invoke over a native-side array ----
		{
			std::vector<perf_color> colors_256(256);
			for(std::size_t i = 0; i < colors_256.size(); ++i)
			{
				const auto byte = static_cast<std::uint8_t>(i & 255u);
				colors_256[i] = {byte, byte, byte, 255};
			}
			dotnet::array<perf_color> arr(colors_256, color_type);
			auto brighten = dotnet::make_method_invoker<perf_color(perf_color)>(type, "Brighten");

			bench("combo: brighten each Color[] element", 50000,
				  [&](std::int64_t n)
				  {
					  for(std::int64_t r = 0; r < n; ++r)
					  {
						  for(std::size_t i = 0; i < arr.size(); ++i)
						  {
							  auto c = arr.get(i);
							  c = brighten(c);
							  arr.set(i, c);
						  }
					  }
				  });
		}

#if DOTNETPP_BACKEND_MONO
		print_summary("mono");
#else
		print_summary("coreclr");
#endif
	}

	dotnet::shutdown();
	return 0;
}
