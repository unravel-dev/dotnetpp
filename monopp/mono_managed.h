#pragma once

/*
 * POD converter layer for monopp, mirroring clrpp's clr_managed:
 * layout-compatible value types are converted through the
 * managed_interface::converter specialization point and registered with
 * register_basic_mono_converter_for_pod.
 */

#include "mono_type_conversion.h"

namespace mono
{
namespace managed_interface
{

#define register_basic_mono_converter_for_pod(native_type_raw, mono_data_type_aligned)                       \
	template <>                                                                                              \
	struct mono_converter<native_type_raw>                                                                   \
	{                                                                                                        \
		using native_type = native_type_raw;                                                                 \
		using managed_type = mono_data_type_aligned;                                                         \
                                                                                                             \
		static_assert(is_mono_valuetype<managed_type>::value,                                                \
					  "basic_mono_converter is only for value types");                                       \
                                                                                                             \
		static auto to_mono(const native_type& obj) -> managed_type                                          \
		{                                                                                                    \
			return managed_interface::converter::convert<native_type, managed_type>(obj);                    \
		}                                                                                                    \
																											 \
		template <typename U>                                                                                \
		static auto from_mono(U obj) -> std::enable_if_t<std::is_same<U, MonoObject*>::value, native_type>   \
		{                                                                                                    \
			assert(check_type_layout<managed_type>(obj) && "Different type layouts");                        \
			void* ptr = mono_object_unbox(obj);                                                              \
			return managed_interface::converter::convert<managed_type, native_type>(                         \
				*reinterpret_cast<managed_type*>(ptr));                                                      \
		}                                                                                                    \
		template <typename U>                                                                                \
		static auto from_mono(const U& obj)                                                                  \
			-> std::enable_if_t<!std::is_same<U, MonoObject*>::value, native_type>                           \
		{                                                                                                    \
			return managed_interface::converter::convert<managed_type, native_type>(obj);                    \
		}                                                                                                    \
	}

struct converter
{
	template <typename src_t, typename dst_t>
	static auto convert(const src_t&) -> dst_t
	{
		static_assert(std::is_same<src_t, dst_t>::value, "Please specialize.");
	}
};

} // namespace managed_interface

} // namespace mono
