#pragma once

#include "dotnet_config.h"
#include "dotnet_object.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_list.h>

namespace dotnet
{

using list_base = mono::mono_list_base;

template <typename T>
using list = mono::mono_list<T>;

} // namespace dotnet

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_list.h>

namespace dotnet
{

using list_base = clr::clr_list_base;

template <typename T>
using list = clr::clr_list<T>;

} // namespace dotnet
#endif
