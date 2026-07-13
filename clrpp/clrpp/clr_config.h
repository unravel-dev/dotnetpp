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

namespace clr
{
template <typename T>
using non_owning_ptr = T*;

inline auto managed_runtime_dir() -> const char*
{
	return CLRPP_MANAGED_DIR;
}

/// Default .NET version (major.minor) targeted by tooling (default
/// runtimeconfig, csproj generation, runtime bundling). Overridable per init
/// via compiler_paths::dotnet_version.
inline auto default_dotnet_version() -> const char*
{
	return "10.0";
}

} // namespace clr
