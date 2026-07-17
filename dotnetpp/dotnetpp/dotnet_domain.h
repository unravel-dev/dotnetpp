#pragma once

#include "dotnet_config.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_domain.h>

namespace dotnet
{

using domain = mono::mono_domain;

} // namespace dotnet

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_domain.h>

namespace dotnet
{

using domain = clr::clr_domain;

} // namespace dotnet
#endif
