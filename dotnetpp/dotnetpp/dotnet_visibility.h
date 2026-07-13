#pragma once

#include "dotnet_config.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_visibility.h>

namespace dotnet
{

using mono::visibility;
using mono::to_string;

} // namespace dotnet

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_visibility.h>

namespace dotnet
{

using clr::visibility;
using clr::to_string;

} // namespace dotnet
#endif
