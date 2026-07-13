#pragma once

#include "dotnet_config.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_jit.h>

namespace dotnet
{

using mono::compiler_paths;
using mono::debugging_config;

using mono::init;
using mono::get_core_assembly_path;
using mono::shutdown;

using mono::compiler_params;
using mono::compile_cmd;

using mono::create_compile_command;
using mono::create_compile_command_detailed;
using mono::create_compile_rsp;
using mono::create_compile_command_detailed_rsp;
using mono::compile;

using mono::get_common_library_names;
using mono::get_common_library_paths;
using mono::get_common_library_names_for_deploy;
using mono::get_common_config_paths;

using mono::get_common_executable_names;
using mono::get_common_executable_paths;

using mono::is_debugger_attached;

} // namespace dotnet

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_jit.h>

namespace dotnet
{

using clr::compiler_paths;
using clr::debugging_config;

using clr::init;
using clr::get_core_assembly_path;
using clr::shutdown;

using clr::compiler_params;
using clr::compile_cmd;

using clr::create_compile_command;
using clr::create_compile_command_detailed;
using clr::create_compile_rsp;
using clr::create_compile_command_detailed_rsp;
using clr::compile;

using clr::get_common_library_names;
using clr::get_common_library_paths;
using clr::get_common_library_names_for_deploy;
using clr::get_common_config_paths;

using clr::get_common_executable_names;
using clr::get_common_executable_paths;

using clr::is_debugger_attached;

/// Name of the managed bridge folder deployed next to the executables.
using clr::managed_runtime_dir;

/// Target .NET version (major.minor) configured at init.
using clr::get_dotnet_version;

} // namespace dotnet
#endif
