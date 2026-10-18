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

#ifndef CLRPP_DOTNET_PREFERRED_VERSION
#define CLRPP_DOTNET_PREFERRED_VERSION "10.0"
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

/// The .NET version (major.minor) the runtime prefers: the newest installed release of its major
/// is started, and a runtime of another major at or above default_dotnet_version() is only a
/// fallback. Set from CMake (CLRPP_DOTNET_PREFERRED_VERSION); overridable per init via
/// compiler_paths::preferred_dotnet_version.
inline auto preferred_dotnet_version() -> const char*
{
	return CLRPP_DOTNET_PREFERRED_VERSION;
}

/// Folder beside the bridge with the framework reference assemblies scripts compile against
/// (ref/net<version>, copied by the bridge build). Only the compiler uses it.
inline auto reference_assemblies_dir_name() -> const char*
{
	return "ref";
}

/// The runtimeconfig the hostfxr host writes beside the bridge's own, pinned to the runtime it
/// selected, when the dotnet root also holds a newer runtime the bridge's own would start.
inline auto pinned_runtimeconfig_name() -> const char*
{
	return "Clrpp.Managed.selected.runtimeconfig.json";
}

/// How clr::init starts the runtime.
enum class runtime_host
{
	/// hostfxr when the dotnet root has one, coreclr otherwise.
	automatic,
	/// hostfxr resolves the runtime from the bridge's runtimeconfig, the way `dotnet` starts apps.
	hostfxr,
	/// The runtime library of the newest shared framework at or above the target version is
	/// loaded directly and handed the assembly list. The only option where no hostfxr ships,
	/// such as the Android runtime packs.
	coreclr
};

} // namespace clr
