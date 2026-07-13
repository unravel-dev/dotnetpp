#pragma once

#include "dotnet_config.h"
#include "dotnet_visibility.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_method.h>

namespace dotnet
{

using method = mono::mono_method;

using mono::reset_method_cache;

} // namespace dotnet

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_method.h>

namespace dotnet
{

using method = clr::clr_method;

using clr::reset_method_cache;

} // namespace dotnet
#endif
