#pragma once

#include "dotnet_config.h"
#include "dotnet_method_invoker.h"
#include "dotnet_property.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_property_invoker.h>

namespace dotnet
{

template <typename T>
using property_invoker = mono::mono_property_invoker<T>;

using mono::make_property_invoker;

} // namespace dotnet

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_property_invoker.h>

namespace dotnet
{

template <typename T>
using property_invoker = clr::clr_property_invoker<T>;

using clr::make_property_invoker;

} // namespace dotnet
#endif
