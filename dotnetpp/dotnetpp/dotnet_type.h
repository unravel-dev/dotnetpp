#pragma once

#include "dotnet_config.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_type.h>

namespace dotnet
{

using type = mono::mono_type;

using mono::reset_type_cache;

} // namespace dotnet

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_type.h>

namespace dotnet
{

using type = clr::clr_type;

using clr::reset_type_cache;

} // namespace dotnet
#endif
