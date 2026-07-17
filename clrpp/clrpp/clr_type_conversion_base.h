#pragma once

#include "clr_bridge.h"
#include "clr_config.h"
#include "clr_object.h"
#include "clr_type.h"
#include "clr_type_traits.h"

namespace clr
{

template <typename T>
auto check_type_layout(const managed_ptr& obj) -> bool
{
	clr_object object(managed_ptr::share(obj.get()));
	const auto& type = object.get_type();
	const auto clr_sz = type.get_sizeof();
	const auto clr_align = type.get_alignof();
	constexpr auto cpp_sz = sizeof(T);
	constexpr auto cpp_align = alignof(T);

	return clr_sz == cpp_sz && clr_align <= cpp_align;
}

/*
 * Primary converter: blittable value types pass through unchanged.
 * managed_type is the representation used when crossing the boundary:
 *   - the value itself for blittable types
 *   - managed_ptr (a GCHandle) for reference/wrapped types (specializations)
 */
template <typename T>
struct clr_converter
{
	using native_type = T;
	using managed_type = T;

	static_assert(is_clr_valuetype<managed_type>::value, "Specialize converter for non-value types");

	static auto to_managed(const native_type& obj) -> managed_type
	{
		return obj;
	}

	// Unbox from a managed object handle (invoker result path).
	static auto from_managed(const managed_ptr& obj) -> native_type
	{
		assert(check_type_layout<managed_type>(obj) && "Different type layouts");
		native_type value{};
		bridge().object_unbox(obj.get(), std::addressof(value), static_cast<int32_t>(sizeof(native_type)));
		return value;
	}

	// Pass-through (internal call argument path).
	template <typename U>
	static auto from_managed(const U& obj)
		-> std::enable_if_t<!std::is_same<U, managed_ptr>::value && !std::is_pointer<U>::value,
							const native_type&>
	{
		return obj;
	}

	template <typename U>
	static auto from_managed(const U& ptr)
		-> std::enable_if_t<!std::is_same<U, managed_ptr>::value && std::is_pointer<U>::value, native_type*>
	{
		return reinterpret_cast<native_type*>(ptr);
	}
};

} // namespace clr
