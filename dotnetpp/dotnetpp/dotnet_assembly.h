#pragma once

#include "dotnet_config.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_assembly.h>

namespace dotnet
{

using assembly = mono::mono_assembly;

} // namespace dotnet

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_assembly.h>

namespace dotnet
{

using assembly = clr::clr_assembly;

} // namespace dotnet
#endif
