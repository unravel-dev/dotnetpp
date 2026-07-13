#pragma once

/*
 * POD converter layer for clrpp, mirroring monopp's mono_managed:
 * layout-compatible value types are converted through the
 * managed_interface::converter specialization point and registered with
 * register_basic_clr_converter_for_pod.
 */

#include "clr_bridge.h"
#include "clr_type_conversion.h"

#include <cassert>
#include <memory>

namespace clr
{
namespace managed_interface
{

struct converter
{
	template <typename src_t, typename dst_t>
	static auto convert(const src_t&) -> dst_t
	{
		static_assert(std::is_same<src_t, dst_t>::value, "Please specialize.");
	}
};

} // namespace managed_interface

/*
 * Converter registration macro, mirroring monopp's
 * register_basic_mono_converter_for_pod. Expand inside namespace clr.
 */
#define register_basic_clr_converter_for_pod(native_type_raw, clr_data_type_aligned)                         \
	template <>                                                                                              \
	struct clr_converter<native_type_raw>                                                                    \
	{                                                                                                        \
		using native_type = native_type_raw;                                                                 \
		using managed_type = clr_data_type_aligned;                                                          \
                                                                                                             \
		static_assert(is_clr_valuetype<managed_type>::value,                                                 \
					  "basic_clr_converter is only for value types");                                        \
                                                                                                             \
		static auto to_mono(const native_type& obj) -> managed_type                                          \
		{                                                                                                    \
			return managed_interface::converter::convert<native_type, managed_type>(obj);                    \
		}                                                                                                    \
                                                                                                             \
		/* unbox from a managed object handle (invoker result path) */                                       \
		template <typename U>                                                                                \
		static auto from_mono(const U& obj)                                                                  \
			-> std::enable_if_t<std::is_same<U, managed_ptr>::value, native_type>                            \
		{                                                                                                    \
			assert(check_type_layout<managed_type>(obj) && "Different type layouts");                        \
			managed_type value{};                                                                            \
			bridge().object_unbox(obj.get(), std::addressof(value),                                          \
								  static_cast<int32_t>(sizeof(managed_type)));                               \
			return managed_interface::converter::convert<managed_type, native_type>(value);                  \
		}                                                                                                    \
		/* pass-through (internal call argument path) */                                                     \
		template <typename U>                                                                                \
		static auto from_mono(const U& obj)                                                                  \
			-> std::enable_if_t<!std::is_same<U, managed_ptr>::value, native_type>                           \
		{                                                                                                    \
			return managed_interface::converter::convert<managed_type, native_type>(obj);                    \
		}                                                                                                    \
	}

} // namespace clr
