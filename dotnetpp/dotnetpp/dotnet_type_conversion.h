#pragma once

#include "dotnet_config.h"
#include "dotnet_object.h"
#include "dotnet_string.h"
#include "dotnet_type.h"
#include "dotnet_type_traits.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_type_conversion.h>

/*
 * Specialization entry point for user defined converters.
 *
 * Alias templates cannot be specialized, so custom conversions must
 * specialize the backend template. Use the macro below so the code stays
 * backend agnostic. Both backends share the same converter protocol
 * (to_mono/from_mono, where "mono" reads as "managed"):
 *
 *   template <>
 *   struct dotnet_converter<my_type>
 *   {
 *       using native_type = my_type;
 *       using managed_type = dotnet::managed_ptr;
 *       static auto to_mono(const native_type&) -> managed_type;
 *       static auto from_mono(const managed_type&) -> native_type;
 *   };
 *
 * For layout-compatible POD pairs, specialize converter inside
 * dotnetpp_backend::managed_interface (see dotnet_register_converter_for_pod).
 */
#define dotnet_converter ::mono::mono_converter

namespace dotnet
{

/// Raw handle to a managed object as seen by converters (MonoObject* on mono).
using managed_ptr = MonoObject*;

/// Converter lookup for use (not specialization - use dotnet_converter for that).
template <typename T>
using converter = mono::mono_converter<T>;

using mono::check_type_layout;

/// Unified managed handle accessor for converter / interop code.
inline auto get_managed_ptr(const object& obj) -> managed_ptr
{
	return obj.get_internal_ptr();
}

} // namespace dotnet

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_type_conversion.h>

#define dotnet_converter ::clr::clr_converter

namespace dotnet
{

using managed_ptr = clr::managed_ptr;

template <typename T>
using converter = clr::clr_converter<T>;

using clr::check_type_layout;

inline auto get_managed_ptr(const object& obj) -> managed_ptr
{
	return obj.get_managed_ptr();
}

} // namespace dotnet
#endif
