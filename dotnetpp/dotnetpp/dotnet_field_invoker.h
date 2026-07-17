#pragma once

#include "dotnet_config.h"
#include "dotnet_field.h"
#include "dotnet_object.h"
#include "dotnet_type_conversion.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_field_invoker.h>

namespace dotnet
{

template <typename T>
using field_invoker = mono::mono_field_invoker<T>;

using mono::make_field_invoker;

using mono::set_field_value;
using mono::get_field_value;

} // namespace dotnet

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_field_invoker.h>

namespace dotnet
{

template <typename T>
using field_invoker = clr::clr_field_invoker<T>;

using clr::make_field_invoker;

using clr::set_field_value;
using clr::get_field_value;

} // namespace dotnet
#endif
