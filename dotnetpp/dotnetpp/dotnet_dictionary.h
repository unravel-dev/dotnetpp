#pragma once

#include "dotnet_config.h"
#include "dotnet_object.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_dictionary.h>

namespace dotnet
{

using dictionary = mono::mono_dictionary;

} // namespace dotnet

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_dictionary.h>

namespace dotnet
{

using dictionary = clr::clr_dictionary;

} // namespace dotnet
#endif
