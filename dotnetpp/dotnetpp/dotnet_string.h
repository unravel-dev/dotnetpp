#pragma once

#include "dotnet_config.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_string.h>

namespace dotnet
{

using string = mono::mono_string;

} // namespace dotnet

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_string.h>

namespace dotnet
{

using string = clr::clr_string;

} // namespace dotnet
#endif
