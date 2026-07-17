#pragma once

#include "clr_config.h"

#include "clr_exception.h"
#include "clr_method.h"
#include "clr_object.h"
#include "clr_type.h"
#include "clr_type_conversion.h"

#include <array>
#include <tuple>
#include <utility>

namespace clr
{

template <typename T>
auto is_compatible_type(const clr_type& type) -> bool
{
	if(type.is_enum())
	{
		return is_compatible_type<T>(type.get_enum_base_type());
	}
	const auto& expected_name = type.get_fullname();
	return types::is_compatible_type<T>(expected_name);
}

template <typename Signature>
auto has_compatible_signature(const clr_method& method) -> bool
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
	compatible &= is_compatible_type<return_type>(expected_rtype);
	if(!compatible)
	{
		// allow cpp return type to be void i.e ignoring it.
		if(!is_compatible_type<void>(expected_rtype))
		{
			return false;
		}
	}
	// Iterate over the argument types without constructing values (argument
	// types are not required to be default constructible). The check keeps
	// the historical const& semantics of the value based iteration.
	clr::for_each_tuple_type<arg_types>(
		[&compatible, &expected_arg_types](auto index)
		{
			using arg_type = const std::tuple_element_t<decltype(index)::value, arg_types>&;
			compatible &= is_compatible_type<arg_type>(expected_arg_types[decltype(index)::value]);
		});

	return compatible;
}

namespace detail
{

// Result retrieval, dispatched on the converter's managed representation.
template <typename T, typename Managed = typename clr_converter<std::decay_t<T>>::managed_type>
struct invoke_result
{
	// Blittable: the managed side writes the value's raw bytes, so the
	// buffer must have the *managed* representation's size/layout - for
	// converted types (e.g. a packed byte color natively, four floats
	// managed) it differs from the native one. Convert on extract.
	using storage_type = Managed;

	static auto prepare(storage_type& storage) -> clr_variant
	{
		clr_variant v;
		v.kind = clr_variant::kind_blob;
		v.size = static_cast<int32_t>(sizeof(storage_type));
		v.data = std::addressof(storage);
		return v;
	}

	static auto extract(storage_type& storage, const clr_variant& v) -> std::decay_t<T>
	{
		if(v.kind == clr_variant::kind_empty)
		{
			return {};
		}
		return clr_converter<std::decay_t<T>>::from_managed(storage);
	}
};

template <typename T>
struct invoke_result<T, managed_ptr>
{
	// handle based: the native result type itself need not be default
	// constructible, only the handle placeholder is.
	using storage_type = managed_ptr;

	static auto prepare(storage_type&) -> clr_variant
	{
		clr_variant v;
		v.kind = clr_variant::kind_object_handle;
		return v;
	}

	static auto extract(storage_type&, const clr_variant& v) -> std::decay_t<T>
	{
		if(v.kind != clr_variant::kind_object_handle || !v.data)
		{
			return clr_converter<std::decay_t<T>>::from_managed(managed_ptr{});
		}
		return clr_converter<std::decay_t<T>>::from_managed(managed_ptr::adopt(v.data));
	}
};

template <typename... Args>
struct variant_pack
{
	// Keeps the converted managed representations alive while their
	// addresses/handles are referenced by the variants.
	std::tuple<typename clr_converter<std::decay_t<Args>>::managed_type...> storage;
	std::array<clr_variant, sizeof...(Args) == 0 ? 1 : sizeof...(Args)> variants;
	int32_t count = static_cast<int32_t>(sizeof...(Args));

	explicit variant_pack(Args... args)
		: storage(clr_converter<std::decay_t<Args>>::to_managed(std::forward<Args>(args))...)
	{
		fill(std::make_index_sequence<sizeof...(Args)>{});
	}

	template <std::size_t... I>
	void fill(std::index_sequence<I...>)
	{
		using discard = int[];
		(void)discard{0, (void(variants[I] = to_clr_variant(std::get<I>(storage))), 0)...};
	}
};

} // namespace detail

template <typename T>
class clr_method_invoker;

template <typename... Args>
class clr_method_invoker<void(Args...)> : public clr_method
{
public:
	void operator()(Args... args)
	{
		invoke(nullptr, std::forward<Args>(args)...);
	}

	void operator()(const clr_object& obj, Args... args)
	{
		invoke(&obj, std::forward<Args>(args)...);
	}

private:
	void invoke(const clr_object* obj, Args... args)
	{
		if(!valid())
		{
			throw clr_exception("NATIVE::Method thunk requested with invalid method");
		}

		clr_handle target = (obj && obj->valid()) ? obj->get_internal_ptr() : nullptr;

		detail::variant_pack<Args...> pack(std::forward<Args>(args)...);

		clr_variant result; // kind_empty - discard
		clr_exception_info_raw ex{};
		bridge().method_invoke(get_internal_ptr(), target, pack.variants.data(), pack.count, &result, &ex);
		throw_if_exception(ex);

		// Managed side may still hand back a handle for non-void methods.
		if(result.kind == clr_variant::kind_object_handle && result.data)
		{
			bridge().free_handle(result.data);
		}
	}

	template <typename Signature>
	friend auto make_method_invoker(const clr_method&, bool) -> clr_method_invoker<Signature>;

	clr_method_invoker(const clr_method& o)
		: clr_method(o)
	{
	}
};

template <typename RetType, typename... Args>
class clr_method_invoker<RetType(Args...)> : public clr_method
{
public:
	auto operator()(Args... args)
	{
		return invoke(nullptr, std::forward<Args>(args)...);
	}

	auto operator()(const clr_object& obj, Args... args)
	{
		return invoke(&obj, std::forward<Args>(args)...);
	}

private:
	auto invoke(const clr_object* obj, Args... args)
	{
		if(!valid())
		{
			throw clr_exception("NATIVE::Method thunk requested with invalid method");
		}

		clr_handle target = (obj && obj->valid()) ? obj->get_internal_ptr() : nullptr;

		detail::variant_pack<Args...> pack(std::forward<Args>(args)...);

		using result_traits = detail::invoke_result<RetType>;
		typename result_traits::storage_type storage{};
		clr_variant result = result_traits::prepare(storage);

		clr_exception_info_raw ex{};
		bridge().method_invoke(get_internal_ptr(), target, pack.variants.data(), pack.count, &result, &ex);
		throw_if_exception(ex);

		return result_traits::extract(storage, result);
	}

	template <typename Signature>
	friend auto make_method_invoker(const clr_method&, bool) -> clr_method_invoker<Signature>;

	clr_method_invoker(const clr_method& o)
		: clr_method(o)
	{
	}
};

template <typename Signature>
auto make_method_invoker(const clr_method& method, bool check_signature = true)
	-> clr_method_invoker<Signature>
{
	if(check_signature && !has_compatible_signature<Signature>(method))
	{
		throw clr_exception("NATIVE::Method thunk requested with incompatible signature");
	}
	return clr_method_invoker<Signature>(method);
}

template <typename Signature>
auto make_method_invoker(const clr_type& type, const std::string& name) -> clr_method_invoker<Signature>
{
	using arg_types = typename function_traits<Signature>::arg_types;
	auto args_result = types::get_args_signature<arg_types>();
	auto all_types_known = args_result.second;

	if(all_types_known)
	{
		auto func = type.get_method(name + "(" + args_result.first + ")");
		return make_method_invoker<Signature>(func);
	}
	else
	{
		constexpr auto arg_count = function_traits<Signature>::arity;
		auto func = type.get_method(name, arg_count);
		return make_method_invoker<Signature>(func);
	}
}

template <typename Signature>
auto make_method_invoker(const clr_object& obj, const std::string& name) -> clr_method_invoker<Signature>
{
	const auto& type = obj.get_type();

	return make_method_invoker<Signature>(type, name);
}

} // namespace clr
