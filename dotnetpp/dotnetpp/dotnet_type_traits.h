#pragma once

#include "dotnet_config.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_type_traits.h>

namespace dotnet
{

template <typename T>
using is_valuetype = mono::is_mono_valuetype<T>;

using mono::function_traits;

using mono::ignore;
using mono::apply;
using mono::for_each;
using mono::for_each_type;
using mono::for_each_tuple_type;

using mono::tag_t;
using mono::type_t;

namespace types = mono::types;

} // namespace dotnet

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_type_traits.h>

namespace dotnet
{

template <typename T>
using is_valuetype = clr::is_clr_valuetype<T>;

using clr::function_traits;

using clr::ignore;
using clr::apply;
using clr::for_each;
using clr::for_each_type;
using clr::for_each_tuple_type;

using clr::tag_t;
using clr::type_t;

namespace types = clr::types;

} // namespace dotnet
#endif
