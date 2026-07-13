#pragma once

#include "dotnet_config.h"
#include "dotnet_exception.h"
#include "dotnet_method.h"
#include "dotnet_object.h"
#include "dotnet_type.h"
#include "dotnet_type_conversion.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_method_invoker.h>

namespace dotnet
{

using mono::is_compatible_type;
using mono::has_compatible_signature;

template <typename Signature>
using method_invoker = mono::mono_method_invoker<Signature>;

using mono::make_method_invoker;

} // namespace dotnet

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_method_invoker.h>

namespace dotnet
{

using clr::is_compatible_type;
using clr::has_compatible_signature;

template <typename Signature>
using method_invoker = clr::clr_method_invoker<Signature>;

using clr::make_method_invoker;

} // namespace dotnet
#endif
