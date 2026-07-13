#pragma once

/*
 * Unified mapping of the POD converter layer: layout-compatible value types
 * converted through managed_interface::converter.
 *
 * dotnetpp_backend is a macro expanding to the active backend namespace
 * (mono or clr). Use it for converter specializations:
 *
 *   namespace dotnetpp_backend::managed_interface {
 *   template<> auto converter::convert(...) -> ...;
 *   }
 *
 * dotnet_register_converter_for_pod expands into the backend namespace
 * internally — do not wrap it in dotnetpp_backend { }.
 */

#include "dotnet_config.h"
#include "dotnet_type_conversion.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_managed.h>

#define dotnetpp_backend mono

namespace dotnet
{

namespace managed_interface
{

using mono::managed_interface::converter;

} // namespace managed_interface

} // namespace dotnet

/*
 * Backend agnostic converter registration macro. Must be used at global
 * scope (it expands to an explicit specialization of the backend converter
 * using qualified names).
 */
#define dotnet_register_converter_for_pod(native_type_raw, managed_data_type_aligned)                        \
    namespace mono                                                                                           \
    {                                                                                                        \
    register_basic_mono_converter_for_pod(native_type_raw, managed_data_type_aligned);                       \
    }

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_managed.h>

#define dotnetpp_backend clr

namespace dotnet
{

namespace managed_interface
{

using clr::managed_interface::converter;

} // namespace managed_interface

} // namespace dotnet

#define dotnet_register_converter_for_pod(native_type_raw, managed_data_type_aligned)                        \
    namespace clr                                                                                            \
    {                                                                                                        \
    register_basic_clr_converter_for_pod(native_type_raw, managed_data_type_aligned);                        \
    }

#endif
