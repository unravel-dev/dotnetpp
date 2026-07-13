#pragma once

#include "dotnet_config.h"
#include "dotnet_visibility.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_field.h>

namespace dotnet
{

using field = mono::mono_field;

using mono::reset_field_cache;

} // namespace dotnet

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_field.h>

namespace dotnet
{

using field = clr::clr_field;

using clr::reset_field_cache;

} // namespace dotnet
#endif
