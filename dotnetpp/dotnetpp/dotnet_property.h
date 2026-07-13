#pragma once

#include "dotnet_config.h"
#include "dotnet_visibility.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_property.h>

namespace dotnet
{

using property = mono::mono_property;

using mono::reset_property_cache;

} // namespace dotnet

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_property.h>

namespace dotnet
{

using property = clr::clr_property;

using clr::reset_property_cache;

} // namespace dotnet
#endif
