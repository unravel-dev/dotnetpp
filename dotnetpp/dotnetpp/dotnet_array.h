#pragma once

#include "dotnet_config.h"
#include "dotnet_object.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_array.h>

namespace dotnet
{

template <typename VectorLike>
using vector_like_wrapper = mono::vector_like_wrapper<VectorLike>;

using array_base = mono::mono_array_base;

template <typename T>
using array = mono::mono_array<T>;

} // namespace dotnet

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_array.h>

namespace dotnet
{

template <typename VectorLike>
using vector_like_wrapper = clr::vector_like_wrapper<VectorLike>;

using array_base = clr::clr_array_base;

template <typename T>
using array = clr::clr_array<T>;

} // namespace dotnet
#endif
