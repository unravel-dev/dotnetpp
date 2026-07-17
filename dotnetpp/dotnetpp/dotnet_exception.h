#pragma once

#include "dotnet_config.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_exception.h>

namespace dotnet
{

using exception = mono::mono_exception;
using thunk_exception = mono::mono_thunk_exception;

using mono::raise_exception;

using mono::stack_frame_info;
using mono::extract_relevant_stack_frame;

} // namespace dotnet

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_exception.h>

namespace dotnet
{

using exception = clr::clr_exception;
using thunk_exception = clr::clr_thunk_exception;

using clr::raise_exception;

using clr::stack_frame_info;
using clr::extract_relevant_stack_frame;

} // namespace dotnet
#endif
