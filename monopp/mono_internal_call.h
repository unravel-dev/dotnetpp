#pragma once

#include "mono_config.h"

#include "mono_type_conversion.h"

BEGIN_MONO_INCLUDE
#include <mono/metadata/loader.h>
END_MONO_INCLUDE

namespace mono
{

template <typename F>
inline void add_internal_call(const std::string& name, F&& func)
{
	mono_add_internal_call(name.c_str(), reinterpret_cast<const void*>(func));
}

struct internal_call_registry
{
	internal_call_registry(const std::string& type)
		: full_typename(type)
	{
	}

	template <typename F>
	inline void add_internal_call(const std::string& name, F&& func)
	{
		mono::add_internal_call(full_typename + "::" + name, std::forward<F>(func));
	}

	std::string full_typename;
};

// Helper traits to handle return types
template <typename R, bool IsVoid = std::is_void<R>::value>
struct return_type_traits;

// Specialization for non-void return types
template <typename R>
struct return_type_traits<R, false>
{
	using return_t = mono_converter<std::decay_t<R>>;
	using managed_return_t = typename return_t::managed_type;

	template <typename Func, typename... Args>
	static auto call(Func func, Args&&... args) -> managed_return_t
	{
		auto result = func(std::forward<Args>(args)...);
		return return_t::to_managed(result);
	}
};

// Specialization for void return types
template <typename R>
struct return_type_traits<R, true>
{
	using managed_return_t = void;

	template <typename Func, typename... Args>
	static void call(Func func, Args&&... args)
	{
		func(std::forward<Args>(args)...);
	}
};

template <typename Signature, Signature& func>
struct mono_jit_internal_call_wrapper;

template <typename R, typename... Args, R (&func)(Args...)>
struct mono_jit_internal_call_wrapper<R(Args...), func>
{
	template <typename T>
	using args_t = mono_converter<std::remove_pointer_t<std::decay_t<T>>>;

	template <typename T>
	using is_ref = std::is_reference<T>;

	template <typename T>
	static constexpr bool pass_by_value =
		!std::is_pointer<T>::value &&
		((is_ref<T>::value && std::is_const<std::remove_reference_t<T>>::value) || !is_ref<T>::value);

	template <typename T>
	using func_args_t = std::conditional_t<pass_by_value<T>, typename args_t<T>::managed_type,
										   typename args_t<T>::managed_type*>;

	using traits = return_type_traits<R>;
	using managed_return_t = typename traits::managed_return_t;

	static auto wrapper(func_args_t<Args>... args) -> managed_return_t
	{
		return traits::call(func, args_t<Args>::from_managed(args)...);
	}
};

/*!
 * Wrap a function for mono::add_internal_call, where automatic type
 * conversion is done through mono_converter. Add your own specialization implementation
 * of this class to support more types.
 */
#define internal_call(func) &mono::mono_jit_internal_call_wrapper<decltype(func), func>::wrapper

} // namespace mono
