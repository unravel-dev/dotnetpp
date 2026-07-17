#pragma once

#include "mono_config.h"

#include "mono_array.h"
// Forward declaration - mono_list converter specializations moved to mono_list.h to break circular dependency
#include "mono_domain.h"
#include "mono_string.h"
#include "mono_type.h"
#include "mono_type_traits.h"
#include "mono_arg.h"

namespace mono
{


template <typename T>
auto check_type_layout(MonoObject* obj) -> bool
{
	mono_object object(obj);
	const auto& type = object.get_type();
	const auto mono_sz = type.get_sizeof();
	const auto mono_align = type.get_alignof();
	constexpr auto cpp_sz = sizeof(T);
	constexpr auto cpp_align = alignof(T);

	return mono_sz == cpp_sz && mono_align <= cpp_align;
}

template <typename T>
struct mono_converter
{
	using native_type = T;
	using managed_type = T;

	static_assert(is_mono_valuetype<managed_type>::value, "Specialize converter for non-value types");

	static auto to_managed(const native_type& obj) -> managed_type
	{
		return obj;
	}

	template <typename U>
	static auto from_managed(U obj) -> std::enable_if_t<std::is_same<U, MonoObject*>::value, native_type>
	{
		assert(check_type_layout<managed_type>(obj) && "Different type layouts");
		void* ptr = mono_object_unbox(obj);
		return *reinterpret_cast<native_type*>(ptr);
	}

	template <typename U>
	static auto from_managed(const U& obj)
		-> std::enable_if_t<!std::is_same<U, MonoObject*>::value && !std::is_pointer<U>::value, const native_type&>
	{
		return obj;
	}

	template <typename U>
	static auto from_managed(const U& ptr)
		-> std::enable_if_t<!std::is_same<U, MonoObject*>::value && std::is_pointer<U>::value, native_type*>
	{
		return reinterpret_cast<native_type*>(ptr);
	}
};


} // namespace mono
