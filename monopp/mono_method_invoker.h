#pragma once

#include "mono_config.h"

#include "mono_exception.h"
#include "mono_method.h"
#include "mono_object.h"
#include "mono_type.h"
#include "mono_type_conversion.h"

#include <tuple>
#include <utility>
#include <array>

namespace mono
{

template <typename T>
auto is_compatible_type(const mono_type& type) -> bool
{
	if(type.is_enum())
	{
		return is_compatible_type<T>(type.get_enum_base_type());
	}
	const auto& expected_name = type.get_fullname();
	return types::is_compatible_type<T>(expected_name);
}

template <typename Signature>
auto has_compatible_signature(const mono_method& method) -> bool
{
	constexpr auto arity = function_traits<Signature>::arity;
	using return_type = typename function_traits<Signature>::return_type;
	using arg_types = typename function_traits<Signature>::arg_types_decayed;
	auto expected_rtype = method.get_return_type();
	const auto& expected_arg_types = method.get_param_types();

	bool compatible = arity == expected_arg_types.size();
	if(!compatible)
	{
		return false;
	}
	if(!is_compatible_type<return_type>(expected_rtype))
	{
		// A void C++ signature may ignore the managed return value (the
		// invoker discards the result); any other mismatch is an error.
		// Exempting void *methods* instead would accept e.g. an int()
		// invoker on a void method and unbox a null result.
		if(!std::is_void<return_type>::value)
		{
			return false;
		}
	}
	// Iterate over the argument types without constructing values (argument
	// types are not required to be default constructible). The check keeps
	// the historical const& semantics of the value based iteration.
	mono::for_each_tuple_type<arg_types>(
		[&compatible, &expected_arg_types](auto index)
		{
			using arg_type = const std::tuple_element_t<decltype(index)::value, arg_types>&;
			compatible &= is_compatible_type<arg_type>(expected_arg_types[decltype(index)::value]);
		});

	return compatible;
}

template <typename T>
class mono_method_invoker;

template <typename... Args>
class mono_method_invoker<void(Args...)> : public mono_method
{
public:
	/// Constructs an invalid invoker (operator() throws); assign one obtained
	/// through make_method_invoker, which performs the signature check.
	mono_method_invoker() = default;

	void operator()(Args... args)
	{
		invoke(nullptr, std::forward<Args>(args)...);
	}

	void operator()(const mono_object& obj, Args... args)
	{
		invoke(&obj, std::forward<Args>(args)...);
	}

private:
	void invoke(const mono_object* obj, Args... args)
	{
		auto method = this->method_;
		if(!method)
		{
			throw mono_exception("NATIVE::Method thunk requested with invalid method");
		}
		MonoObject* object = nullptr;
		if(obj && obj->valid())
		{
			object = obj->get_internal_ptr();
			if(object)
			{
				method = mono_object_get_virtual_method(object, method);
			}
		}
		auto tup = std::make_tuple(mono_converter<std::decay_t<Args>>::to_managed(std::forward<Args>(args))...);

		const auto& param_types = this->get_param_types();
		auto inv = [&](auto... args)
		{
			constexpr size_t N = sizeof...(args);
			
			// Create args array with correct parameter types (C++14 compatible)
			std::array<void*, N> argsv;
			size_t idx = 0;
			
			// C++14 compatible parameter pack expansion using initializer list
			std::initializer_list<int> dummy = {(
				argsv[idx] = to_managed_arg(args, (idx < param_types.size()) ? param_types[idx] : mono_type{}),
				++idx,
				0
			)...};
			(void)dummy; // Suppress unused variable warning

			MonoObject* ex = nullptr;
			mono_runtime_invoke(method, object, argsv.data(), &ex);
			if(ex)
			{
				throw mono_thunk_exception(ex);
			}
		};

		mono::apply(inv, tup);
	}

	template <typename Signature>
	friend auto make_method_invoker(const mono_method&, bool) -> mono_method_invoker<Signature>;

	mono_method_invoker(const mono_method& o)
		: mono_method(o)
	{
	}
};

template <typename RetType, typename... Args>
class mono_method_invoker<RetType(Args...)> : public mono_method
{
public:
	/// Constructs an invalid invoker (operator() throws); assign one obtained
	/// through make_method_invoker, which performs the signature check.
	mono_method_invoker() = default;

	auto operator()(Args... args)
	{
		return invoke(nullptr, std::forward<Args>(args)...);
	}

	auto operator()(const mono_object& obj, Args... args)
	{
		return invoke(&obj, std::forward<Args>(args)...);
	}

private:
	auto invoke(const mono_object* obj, Args... args)
	{
		auto method = this->method_;
		if(!method)
		{
			throw mono_exception("NATIVE::Method thunk requested with invalid method");
		}

		MonoObject* object = nullptr;
		if(obj && obj->valid())
		{
			object = obj->get_internal_ptr();
			if(object)
			{
				method = mono_object_get_virtual_method(object, method);
			}
		}
		auto tup = std::make_tuple(mono_converter<std::decay_t<Args>>::to_managed(std::forward<Args>(args))...);
		const auto& param_types = this->get_param_types();
		auto inv = [&](auto... args)
		{
			constexpr size_t N = sizeof...(args);
			
			// Create args array with correct parameter types (C++14 compatible)
			std::array<void*, N> argsv;
			size_t idx = 0;
			
			// C++14 compatible parameter pack expansion using initializer list
			std::initializer_list<int> dummy = {(
				argsv[idx] = to_managed_arg(args, (idx < param_types.size()) ? param_types[idx] : mono_type{}),
				++idx,
				0
			)...};
			(void)dummy; // Suppress unused variable warning

			MonoObject* ex = nullptr;
			auto result = mono_runtime_invoke(method, object, argsv.data(), &ex);
			if(ex)
			{
				throw mono_thunk_exception(ex);
			}

			return result;
		};

		auto result = mono::apply(inv, tup);
		return mono_converter<std::decay_t<RetType>>::from_managed(std::move(result));
	}

	template <typename Signature>
	friend auto make_method_invoker(const mono_method&, bool) -> mono_method_invoker<Signature>;

	mono_method_invoker(const mono_method& o)
		: mono_method(o)
	{
	}
};

template <typename Signature>
auto make_method_invoker(const mono_method& method, bool check_signature = true)
	-> mono_method_invoker<Signature>
{
	if(check_signature && !has_compatible_signature<Signature>(method))
	{
		throw mono_exception("NATIVE::Method thunk requested with incompatible signature");
	}
	return mono_method_invoker<Signature>(method);
}

template <typename Signature>
auto make_method_invoker(const mono_type& type, const std::string& name) -> mono_method_invoker<Signature>
{
	using arg_types = typename function_traits<Signature>::arg_types;
	auto args_result = types::get_args_signature<arg_types>();
	constexpr auto arg_count = function_traits<Signature>::arity;

	if(args_result.second)
	{
		// Exact-signature lookup first: it selects the right overload when
		// several share the arity. It can still miss for parameters whose
		// managed type has no C++ name mapping (e.g. an enum declared as its
		// underlying integer on the C++ side), so fall back to name+arity -
		// the signature compatibility check in make_method_invoker still
		// validates the fallback.
		try
		{
			auto func = type.get_method(name + "(" + args_result.first + ")");
			return make_method_invoker<Signature>(func);
		}
		catch(const mono_exception&)
		{
		}
	}

	auto func = type.get_method(name, arg_count);
	return make_method_invoker<Signature>(func);
}

template <typename Signature>
auto make_method_invoker(const mono_object& obj, const std::string& name) -> mono_method_invoker<Signature>
{
	const auto& type = obj.get_type();

	return make_method_invoker<Signature>(type, name);
}

} // namespace mono
