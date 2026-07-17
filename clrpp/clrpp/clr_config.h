#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef CLRPP_MANAGED_DIR
#define CLRPP_MANAGED_DIR "clrpp"
#endif

#ifndef CLRPP_DOTNET_VERSION
#define CLRPP_DOTNET_VERSION "9.0"
#endif

namespace clr
{
template <typename T>
using non_owning_ptr = T*;

inline auto managed_runtime_dir() -> const char*
{
	return CLRPP_MANAGED_DIR;
}

/// Default .NET version (major.minor) targeted by tooling (default
/// runtimeconfig, csproj generation, runtime bundling). Set from CMake
/// (CLRPP_DOTNET_VERSION, which also drives the bridge TargetFramework);
/// overridable per init via compiler_paths::dotnet_version.
inline auto default_dotnet_version() -> const char*
{
	return CLRPP_DOTNET_VERSION;
}

} // namespace clr
