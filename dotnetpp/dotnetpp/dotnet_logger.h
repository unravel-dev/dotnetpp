#pragma once

#include "dotnet_config.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_logger.h>

namespace dotnet
{

using mono::log_handler;
using mono::set_log_handler;
using mono::get_log_handler;
using mono::log_message;

} // namespace dotnet

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_logger.h>

namespace dotnet
{

using clr::log_handler;
using clr::set_log_handler;
using clr::get_log_handler;
using clr::log_message;

} // namespace dotnet
#endif
