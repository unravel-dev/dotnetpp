#pragma once

#include "dotnet_config.h"
#include "dotnet_type.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_object.h>

namespace dotnet
{

using object = mono::mono_object;

template <typename T>
auto box_value(const T& value, const type& t) -> object
{
    return mono::mono_box_value<T>(value, t);
}

template <typename T>
auto unbox_value(const object& obj) -> T
{
    return mono::mono_unbox_value<T>(obj);
}

} // namespace dotnet

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_object.h>

namespace dotnet
{

using object = clr::clr_object;

template <typename T>
auto box_value(const T& value, const type& t) -> object
{
    return clr::clr_box_value<T>(value, t);
}

template <typename T>
auto unbox_value(const object& obj) -> T
{
    return clr::clr_unbox_value<T>(obj);
}

} // namespace dotnet
#endif
