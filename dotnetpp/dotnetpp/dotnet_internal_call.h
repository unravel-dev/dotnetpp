#pragma once

#include "dotnet_config.h"
#include "dotnet_type_conversion.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_internal_call.h>

namespace dotnet
{

using mono::add_internal_call;
using mono::internal_call_registry;

/// Icall IL weaving is a coreclr-only compile step (mono handles
/// [MethodImpl(InternalCall)] natively); successful no-op on this backend.
inline auto weave_assembly(const std::string&) -> bool
{
	return true;
}

} // namespace dotnet

/*
 * Wrap a function for dotnet::add_internal_call, with automatic type
 * conversion through dotnet_converter. Backend agnostic version of the
 * monopp internal_call() macro (which remains available on the mono backend).
 */
#define dotnet_internal_call(func) internal_call(func)

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_internal_call.h>

namespace dotnet
{

using clr::add_internal_call;
using clr::internal_call_registry;
using clr::weave_assembly;

} // namespace dotnet

#define dotnet_internal_call(func) clr_internal_call(func)

#endif
