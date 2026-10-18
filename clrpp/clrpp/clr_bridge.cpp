#include "clr_bridge.h"
#include "clr_exception.h"
#include "clr_internal_call.h"
#include "clr_logger.h"
#include "clr_path_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace clr
{

// ---------------------------------------------------------------------------
// Minimal hostfxr declarations (mirrors dotnet/runtime hostfxr.h /
// coreclr_delegates.h so no nethost package is required at build time).
// ---------------------------------------------------------------------------

#ifdef _WIN32
using char_t = wchar_t;
#define HOSTFXR_CALLTYPE __cdecl
#define CORECLR_DELEGATE_CALLTYPE __stdcall
#else
using char_t = char;
#define HOSTFXR_CALLTYPE
#define CORECLR_DELEGATE_CALLTYPE
#endif

namespace
{
using hostfxr_handle = void*;

struct hostfxr_initialize_parameters
{
	size_t size;
	const char_t* host_path;
	const char_t* dotnet_root;
};

enum hostfxr_delegate_type
{
	hdt_com_activation = 0,
	hdt_load_in_memory_assembly = 1,
	hdt_winrt_activation = 2,
	hdt_com_register = 3,
	hdt_com_unregister = 4,
	hdt_load_assembly_and_get_function_pointer = 5,
	hdt_get_function_pointer = 6,
	hdt_load_assembly = 7,
	hdt_load_assembly_bytes = 8,
};

using hostfxr_initialize_for_runtime_config_fn = int32_t(HOSTFXR_CALLTYPE*)(const char_t*,
																			const hostfxr_initialize_parameters*,
																			hostfxr_handle*);
using hostfxr_get_runtime_delegate_fn = int32_t(HOSTFXR_CALLTYPE*)(hostfxr_handle, hostfxr_delegate_type, void**);
using hostfxr_close_fn = int32_t(HOSTFXR_CALLTYPE*)(hostfxr_handle);

using load_assembly_and_get_function_pointer_fn = int32_t(CORECLR_DELEGATE_CALLTYPE*)(const char_t* assembly_path,
																					  const char_t* type_name,
																					  const char_t* method_name,
																					  const char_t* delegate_type_name,
																					  void* reserved, void** delegate);

const char_t* const UNMANAGEDCALLERSONLY_METHOD = reinterpret_cast<const char_t*>(-1);

// ---------------------------------------------------------------------------
// Minimal coreclr host declarations (mirrors dotnet/runtime coreclrhost.h).
// Strings are UTF-8 on every platform.
// ---------------------------------------------------------------------------

#if defined(_WIN32) && defined(_M_IX86)
#define CORECLR_HOST_CALLTYPE __stdcall
#else
#define CORECLR_HOST_CALLTYPE
#endif

using coreclr_initialize_fn = int32_t(CORECLR_HOST_CALLTYPE*)(const char* exe_path,
															  const char* app_domain_friendly_name,
															  int32_t property_count,
															  const char** property_keys,
															  const char** property_values,
															  void** host_handle,
															  uint32_t* domain_id);
using coreclr_create_delegate_fn = int32_t(CORECLR_HOST_CALLTYPE*)(void* host_handle,
																   uint32_t domain_id,
																   const char* entry_point_assembly_name,
																   const char* entry_point_type_name,
																   const char* entry_point_method_name,
																   void** delegate);

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct bridge_state
{
	bool alive = false;
	bridge_detail::exports table{};
	runtime_host host = runtime_host::automatic;

	void* hostfxr_lib = nullptr;
	hostfxr_close_fn close_fn = nullptr;
	hostfxr_handle context = nullptr;

	// coreclr host: the runtime lives until the process exits, so a later initialize (an
	// in-process restart) binds the bridge again instead of starting a second runtime.
	void* coreclr_lib = nullptr;
	void* coreclr_handle = nullptr;
	uint32_t coreclr_domain_id = 0;
	coreclr_create_delegate_fn create_delegate_fn = nullptr;

	std::string managed_assembly_path;
	std::string dotnet_root;
	std::string dotnet_version = default_dotnet_version();
};

auto state() -> bridge_state&
{
	static bridge_state instance;
	return instance;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

auto load_library(const std::string& path) -> void*
{
#ifdef _WIN32
	return reinterpret_cast<void*>(LoadLibraryW(path_utils::to_native_path(path).c_str()));
#else
	return dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
#endif
}

auto get_symbol(void* lib, const char* name) -> void*
{
#ifdef _WIN32
	return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(lib), name));
#else
	return dlsym(lib, name);
#endif
}

auto hostfxr_library_name() -> std::string
{
#ifdef _WIN32
	return "hostfxr.dll";
#elif defined(__APPLE__)
	return "libhostfxr.dylib";
#else
	return "libhostfxr.so";
#endif
}

auto coreclr_library_name() -> std::string
{
#ifdef _WIN32
	return "coreclr.dll";
#elif defined(__APPLE__)
	return "libcoreclr.dylib";
#else
	return "libcoreclr.so";
#endif
}

/// Separator of the runtime's path-list properties (TRUSTED_PLATFORM_ASSEMBLIES and friends).
constexpr char PATH_LIST_SEPARATOR =
#ifdef _WIN32
	';';
#else
	':';
#endif

/// Paths handed to the runtime use native separators: forward slashes on Windows leak into the
/// runtime's assembly paths and make coreclr initialization fail with E_INVALIDARG.
auto to_runtime_path(std::string path) -> std::string
{
#ifdef _WIN32
	std::replace(path.begin(), path.end(), '/', '\\');
#endif
	return path;
}

auto to_hex(int32_t value) -> std::string
{
	char buffer[16]{};
	std::snprintf(buffer, sizeof(buffer), "0x%08X", static_cast<uint32_t>(value));
	return buffer;
}

// Native callback installed as the managed log sink.
void CLRPP_CALLTYPE native_log_callback(const char* message, const char* category)
{
	log_message(message ? message : "", category ? category : "default");
}

// Resolver handed to Clrpp.InternalCalls.
auto CLRPP_CALLTYPE native_icall_resolver(const char* name) -> void*
{
	return find_internal_call(name ? name : "");
}

auto CLRPP_CALLTYPE native_pending_exception_query() -> const char*
{
	return consume_pending_exception();
}

void write_default_runtimeconfig(const std::string& path, const std::string& version)
{
	std::ofstream file(path.c_str());
	file << "{\n"
		 << "  \"runtimeOptions\": {\n"
		 << "    \"tfm\": \"net" << version << "\",\n"
		 << "    \"rollForward\": \"LatestMajor\",\n"
		 << "    \"framework\": {\n"
		 << "      \"name\": \"Microsoft.NETCore.App\",\n"
		 << "      \"version\": \"" << version << ".0\"\n"
		 << "    }\n"
		 << "  }\n"
		 << "}\n";
}

} // namespace

// ---------------------------------------------------------------------------
// Public bridge access
// ---------------------------------------------------------------------------

auto bridge() -> const bridge_detail::exports&
{
	auto& s = state();
	if(!s.alive)
	{
		throw clr_exception("NATIVE::clrpp runtime is not initialized");
	}
	return s.table;
}

auto bridge_alive() -> bool
{
	return state().alive;
}

auto take_string(const char* str) -> std::string
{
	if(!str)
	{
		return {};
	}

	std::string result(str);
	if(bridge_alive())
	{
		state().table.free_string(str);
	}
	return result;
}

auto managed_ptr::adopt(clr_handle raw) -> managed_ptr
{
	managed_ptr result;
	if(raw)
	{
		result.handle_ = std::shared_ptr<void>(raw,
											   [](void* h)
											   {
												   if(bridge_alive())
												   {
													   state().table.free_handle(h);
												   }
											   });
	}
	return result;
}

auto managed_ptr::share(clr_handle raw) -> managed_ptr
{
	if(!raw || !bridge_alive())
	{
		return {};
	}
	return adopt(state().table.duplicate_handle(raw));
}

// ---------------------------------------------------------------------------
// Runtime bootstrap (called from clr::init / clr::shutdown)
// ---------------------------------------------------------------------------

namespace bridge_detail
{

namespace
{
// A dotnet root is only usable when it actually contains a versioned
// host/fxr directory with the hostfxr library. Merely checking that the
// root directory exists is not enough: e.g. on Ubuntu the Microsoft feed
// installs to /usr/share/dotnet while the distro packages use
// /usr/lib/dotnet, and an empty leftover directory at either location
// must not shadow the real installation.
auto has_hostfxr(const std::string& root) -> bool
{
	auto fxr_base = path_utils::path_join(path_utils::path_join(root, "host"), "fxr");
	auto fxr_dir = path_utils::pick_highest_version_dir(fxr_base);
	if(fxr_dir.empty())
	{
		return false;
	}
	return path_utils::path_exists(path_utils::path_join(fxr_dir, hostfxr_library_name()));
}
} // namespace

auto locate_dotnet_root(const std::string& override_root) -> std::string
{
	// Explicit override and DOTNET_ROOT express user intent - honor them
	// as-is and let initialization surface a clear error if they are wrong.
	if(!override_root.empty())
	{
		return override_root;
	}

	auto env_root = path_utils::get_env("DOTNET_ROOT");
	if(!env_root.empty())
	{
		return env_root;
	}

	std::vector<std::string> candidates;
#ifdef _WIN32
	auto program_files = path_utils::get_env("ProgramFiles");
	if(!program_files.empty())
	{
		candidates.push_back(path_utils::path_join(program_files, "dotnet"));
	}
	candidates.push_back("C:/Program Files/dotnet");
#elif defined(__APPLE__)
	candidates.push_back("/usr/local/share/dotnet");
	candidates.push_back("/opt/homebrew/opt/dotnet/libexec"); // brew (arm64)
	candidates.push_back("/usr/local/opt/dotnet/libexec");    // brew (x64)
#else
	candidates.push_back("/usr/share/dotnet"); // Microsoft package feed
	candidates.push_back("/usr/lib/dotnet");   // Debian/Ubuntu distro packages
	candidates.push_back("/usr/lib64/dotnet"); // Fedora/RHEL distro packages
	// Snap: classic `dotnet-sdk` uses current/ as DOTNET_ROOT; versioned
	// snaps keep the host under current/usr/lib/dotnet.
	candidates.push_back("/snap/dotnet-sdk/current");
	candidates.push_back("/snap/dotnet-sdk/current/usr/lib/dotnet");
	candidates.push_back("/snap/dotnet-sdk-90/current");
	candidates.push_back("/snap/dotnet-sdk-90/current/usr/lib/dotnet");
	candidates.push_back("/snap/dotnet-sdk-100/current");
	candidates.push_back("/snap/dotnet-sdk-100/current/usr/lib/dotnet");
	candidates.push_back("/var/lib/snapd/snap/dotnet-sdk/current");
	candidates.push_back("/var/lib/snapd/snap/dotnet-sdk/current/usr/lib/dotnet");
	candidates.push_back("/var/lib/snapd/snap/dotnet-sdk-90/current");
	candidates.push_back("/var/lib/snapd/snap/dotnet-sdk-90/current/usr/lib/dotnet");
	candidates.push_back("/var/lib/snapd/snap/dotnet-sdk-100/current");
	candidates.push_back("/var/lib/snapd/snap/dotnet-sdk-100/current/usr/lib/dotnet");
#endif
	auto home = path_utils::get_env(
#ifdef _WIN32
		"USERPROFILE"
#else
		"HOME"
#endif
	);
	if(!home.empty())
	{
		candidates.push_back(path_utils::path_join(home, ".dotnet")); // dotnet-install scripts
	}

	for(const auto& candidate : candidates)
	{
		if(has_hostfxr(candidate))
		{
			return candidate;
		}
	}

	// Nothing validated - return the first platform default so the caller's
	// error message points at a sensible location.
	return candidates.front();
}

namespace
{
using bootstrap_fn = int32_t(CLRPP_CALLTYPE*)(void**, int32_t);

/// Finds Clrpp.Managed.dll. The bridge and its dependencies (Mono.Cecil, Microsoft.Diagnostics.*)
/// live together in one directory; probe, in order: the explicit assembly_dir, a managed_dir
/// subfolder next to the executable, the executable directory itself, then the same two relative
/// to the working directory.
auto locate_bridge(const std::string& assembly_dir,
				   const std::string& managed_dir,
				   std::string& bridge_dir,
				   std::string& managed_dll) -> bool
{
	std::vector<std::string> candidate_dirs;
	const std::string runtime_dir = managed_dir.empty() ? managed_runtime_dir() : managed_dir;
	if(!assembly_dir.empty())
	{
		candidate_dirs.push_back(assembly_dir);
		candidate_dirs.push_back(path_utils::path_join(assembly_dir, runtime_dir));
	}
	auto exe_path = path_utils::current_executable_path();
	if(!exe_path.empty())
	{
		auto slash = exe_path.find_last_of("/\\");
		if(slash != std::string::npos)
		{
			auto exe_dir = exe_path.substr(0, slash);
			candidate_dirs.push_back(path_utils::path_join(exe_dir, runtime_dir));
			candidate_dirs.push_back(exe_dir);
		}
	}
	candidate_dirs.push_back(path_utils::path_join(path_utils::current_directory(), runtime_dir));
	candidate_dirs.push_back(path_utils::current_directory());

	for(const auto& dir : candidate_dirs)
	{
		auto candidate = path_utils::path_join(dir, "Clrpp.Managed.dll");
		if(path_utils::path_exists(candidate))
		{
			bridge_dir = dir;
			managed_dll = candidate;
			return true;
		}
	}

	std::string tried;
	for(const auto& dir : candidate_dirs)
	{
		tried += (tried.empty() ? "" : ", ") + dir;
	}
	log_message("clrpp: Clrpp.Managed.dll not found; searched: " + tried, "error");
	return false;
}

/// The newest released Microsoft.NETCore.App at or above the target major, like hostfxr's
/// LatestMajor roll-forward. Preview builds are skipped, as hostfxr skips them by default.
auto pick_shared_framework(const std::string& dotnet_root, const std::string& minimum_version) -> std::string
{
	const auto base = path_utils::path_join(path_utils::path_join(dotnet_root, "shared"), "Microsoft.NETCore.App");
	const auto minimum = path_utils::parse_version(minimum_version);
	std::string best;
	std::vector<int> best_version;
	for(const auto& name : path_utils::list_subdirectories(base))
	{
		const auto version = path_utils::parse_version(name);
		const bool is_preview = name.find('-') != std::string::npos;
		const bool is_too_old = !minimum.empty() && !version.empty() && version[0] < minimum[0];
		if(version.empty() || is_preview || is_too_old)
		{
			continue;
		}
		if(best.empty() ||
		   std::lexicographical_compare(best_version.begin(), best_version.end(), version.begin(), version.end()))
		{
			best = name;
			best_version = version;
		}
	}
	return best.empty() ? std::string{} : path_utils::path_join(base, best);
}

/// Appends every assembly of dir to a path list, skipping simple names already listed: the first
/// directory wins, so the framework's own assemblies take precedence over the bridge's copies.
void append_assemblies(const std::string& dir, std::set<std::string>& listed_names, std::string& list)
{
	for(const auto& file : path_utils::list_files(dir, ".dll"))
	{
		std::string simple_name = file;
		std::transform(simple_name.begin(),
					   simple_name.end(),
					   simple_name.begin(),
					   [](unsigned char c)
					   {
						   return static_cast<char>(std::tolower(c));
					   });
		if(!listed_names.insert(simple_name).second)
		{
			continue;
		}
		if(!list.empty())
		{
			list += PATH_LIST_SEPARATOR;
		}
		list += to_runtime_path(path_utils::path_join(dir, file));
	}
}

/// The configProperties of a runtimeconfig (name: plain value pairs), which hostfxr passes to the
/// runtime as properties.
auto read_config_properties(const std::string& runtimeconfig) -> std::vector<std::pair<std::string, std::string>>
{
	std::vector<std::pair<std::string, std::string>> properties;
	std::ifstream file(runtimeconfig.c_str());
	const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	const auto section = text.find("\"configProperties\"");
	const auto body_begin = section == std::string::npos ? std::string::npos : text.find('{', section);
	const auto body_end = body_begin == std::string::npos ? std::string::npos : text.find('}', body_begin);
	if(body_end == std::string::npos)
	{
		return properties;
	}
	const std::string body = text.substr(body_begin + 1, body_end - body_begin - 1);
	size_t cursor = 0;
	for(;;)
	{
		const auto key_begin = body.find('"', cursor);
		const auto key_end = key_begin == std::string::npos ? std::string::npos : body.find('"', key_begin + 1);
		const auto colon = key_end == std::string::npos ? std::string::npos : body.find(':', key_end);
		const auto value_begin =
			colon == std::string::npos ? std::string::npos : body.find_first_not_of(" \t\r\n", colon + 1);
		if(value_begin == std::string::npos)
		{
			break;
		}
		const bool is_string = body[value_begin] == '"';
		const auto value_end =
			is_string ? body.find('"', value_begin + 1) : body.find_first_of(", \t\r\n", value_begin);
		const auto value_length = (value_end == std::string::npos ? body.size() : value_end) - value_begin;
		auto value = is_string ? body.substr(value_begin + 1, value_length - 1) : body.substr(value_begin, value_length);
		properties.emplace_back(body.substr(key_begin + 1, key_end - key_begin - 1), std::move(value));
		if(value_end == std::string::npos)
		{
			break;
		}
		cursor = value_end + 1;
	}
	return properties;
}

/// Starts the runtime through hostfxr from the bridge's runtimeconfig and binds Bootstrap.
auto start_with_hostfxr(const std::string& managed_dll,
						const std::string& runtimeconfig,
						const std::string& dotnet_root,
						bool is_root_overridden) -> bootstrap_fn
{
	auto& s = state();
	auto fxr_base = path_utils::path_join(path_utils::path_join(dotnet_root, "host"), "fxr");
	auto fxr_dir = path_utils::pick_highest_version_dir(fxr_base);
	if(fxr_dir.empty())
	{
		log_message("clrpp: hostfxr not found under " + fxr_base, "error");
		return nullptr;
	}

	auto hostfxr_path = path_utils::path_join(fxr_dir, hostfxr_library_name());
	s.hostfxr_lib = load_library(hostfxr_path);
	if(!s.hostfxr_lib)
	{
		log_message("clrpp: failed to load " + hostfxr_path, "error");
		return nullptr;
	}

	auto init_fn = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
		get_symbol(s.hostfxr_lib, "hostfxr_initialize_for_runtime_config"));
	auto get_delegate_fn = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
		get_symbol(s.hostfxr_lib, "hostfxr_get_runtime_delegate"));
	s.close_fn = reinterpret_cast<hostfxr_close_fn>(get_symbol(s.hostfxr_lib, "hostfxr_close"));

	if(!init_fn || !get_delegate_fn || !s.close_fn)
	{
		log_message("clrpp: hostfxr exports missing in " + hostfxr_path, "error");
		return nullptr;
	}

	auto config_path = path_utils::to_native_path(runtimeconfig);

	// Only pass explicit parameters when the caller overrides the dotnet
	// root; otherwise let hostfxr use its own detection.
	auto root_path = path_utils::to_native_path(to_runtime_path(dotnet_root));
	auto host_path = path_utils::to_native_path(path_utils::current_executable_path());

	hostfxr_initialize_parameters params{};
	params.size = sizeof(params);
	params.host_path = host_path.empty() ? nullptr : host_path.c_str();
	params.dotnet_root = root_path.c_str();

	int32_t rc = init_fn(config_path.c_str(), is_root_overridden ? &params : nullptr, &s.context);
	// 0 = success, 1 = success_host_already_initialized, 2 = success_different_runtime_properties
	if(rc < 0 || rc > 2 || s.context == nullptr)
	{
		log_message("clrpp: hostfxr_initialize_for_runtime_config failed with 0x" +
						std::to_string(rc),
					"error");
		return nullptr;
	}

	void* load_assembly_ptr = nullptr;
	rc = get_delegate_fn(s.context, hdt_load_assembly_and_get_function_pointer, &load_assembly_ptr);
	if(rc != 0 || !load_assembly_ptr)
	{
		log_message("clrpp: failed to acquire load_assembly_and_get_function_pointer", "error");
		return nullptr;
	}

	auto load_assembly_and_get_fn =
		reinterpret_cast<load_assembly_and_get_function_pointer_fn>(load_assembly_ptr);

	bootstrap_fn bootstrap = nullptr;
	auto dll_path = path_utils::to_native_path(managed_dll);
	auto type_name = path_utils::to_native_path("Clrpp.Bridge, Clrpp.Managed");
	auto method_name = path_utils::to_native_path("Bootstrap");

	rc = load_assembly_and_get_fn(dll_path.c_str(), type_name.c_str(), method_name.c_str(),
								  UNMANAGEDCALLERSONLY_METHOD, nullptr,
								  reinterpret_cast<void**>(&bootstrap));
	if(rc != 0 || !bootstrap)
	{
		log_message("clrpp: failed to bind Clrpp.Bridge.Bootstrap (hr=" + std::to_string(rc) + ")",
					"error");
		return nullptr;
	}
	return bootstrap;
}

/// Starts the runtime by loading coreclr from a shared framework of the dotnet root and handing it
/// the properties hostfxr would build: the framework and bridge assemblies, the native search
/// paths and the runtimeconfig's configProperties. Then binds Bootstrap.
auto start_with_coreclr(const std::string& bridge_dir,
						const std::string& runtimeconfig,
						const std::string& dotnet_root) -> bootstrap_fn
{
	auto& s = state();
	if(!s.coreclr_handle)
	{
		const auto framework_dir = pick_shared_framework(dotnet_root, s.dotnet_version);
		if(framework_dir.empty())
		{
			log_message("clrpp: no Microsoft.NETCore.App " + s.dotnet_version + " or newer under " +
							path_utils::path_join(dotnet_root, "shared"),
						"error");
			return nullptr;
		}

		const auto library_path = path_utils::path_join(framework_dir, coreclr_library_name());
		s.coreclr_lib = load_library(library_path);
		if(!s.coreclr_lib)
		{
			log_message("clrpp: failed to load " + library_path, "error");
			return nullptr;
		}

		auto initialize_fn =
			reinterpret_cast<coreclr_initialize_fn>(get_symbol(s.coreclr_lib, "coreclr_initialize"));
		s.create_delegate_fn =
			reinterpret_cast<coreclr_create_delegate_fn>(get_symbol(s.coreclr_lib, "coreclr_create_delegate"));
		if(!initialize_fn || !s.create_delegate_fn)
		{
			log_message("clrpp: coreclr exports missing in " + library_path, "error");
			return nullptr;
		}

		std::set<std::string> listed_names;
		std::string trusted_assemblies;
		append_assemblies(framework_dir, listed_names, trusted_assemblies);
		append_assemblies(bridge_dir, listed_names, trusted_assemblies);

		const auto runtime_bridge_dir = to_runtime_path(bridge_dir);
		std::vector<std::pair<std::string, std::string>> properties{
			{"TRUSTED_PLATFORM_ASSEMBLIES", trusted_assemblies},
			{"APP_PATHS", runtime_bridge_dir},
			{"NATIVE_DLL_SEARCH_DIRECTORIES",
			 to_runtime_path(framework_dir) + PATH_LIST_SEPARATOR + runtime_bridge_dir},
			{"APP_CONTEXT_BASE_DIRECTORY", to_runtime_path(path_utils::path_join(bridge_dir, ""))},
		};
		for(auto& property : read_config_properties(runtimeconfig))
		{
			properties.push_back(std::move(property));
		}

		std::vector<const char*> keys;
		std::vector<const char*> values;
		for(const auto& property : properties)
		{
			keys.push_back(property.first.c_str());
			values.push_back(property.second.c_str());
		}

		const auto exe_path = path_utils::current_executable_path();
		const int32_t hr = initialize_fn(exe_path.c_str(),
										 "clrpp",
										 static_cast<int32_t>(properties.size()),
										 keys.data(),
										 values.data(),
										 &s.coreclr_handle,
										 &s.coreclr_domain_id);
		if(hr < 0 || !s.coreclr_handle)
		{
			s.coreclr_handle = nullptr;
			log_message("clrpp: coreclr_initialize failed with " + to_hex(hr), "error");
			return nullptr;
		}
		log_message("clrpp: coreclr started from " + framework_dir, "trace");
	}

	void* bootstrap = nullptr;
	const int32_t hr = s.create_delegate_fn(s.coreclr_handle,
											s.coreclr_domain_id,
											"Clrpp.Managed",
											"Clrpp.Bridge",
											"Bootstrap",
											&bootstrap);
	if(hr < 0 || !bootstrap)
	{
		log_message("clrpp: failed to bind Clrpp.Bridge.Bootstrap (hr=" + to_hex(hr) + ")", "error");
		return nullptr;
	}
	return reinterpret_cast<bootstrap_fn>(bootstrap);
}

/// Releases what a failed start leaves behind. The coreclr runtime stays: it cannot be stopped
/// and started again within a process.
void release_failed_start()
{
	auto& s = state();
	if(s.context && s.close_fn)
	{
		s.close_fn(s.context);
	}
	s.context = nullptr;
}
} // namespace

auto initialize(const std::string& assembly_dir,
				const std::string& dotnet_root_override,
				const std::string& managed_dir,
				const std::string& dotnet_version,
				runtime_host host) -> bool
{
	auto& s = state();
	if(s.alive)
	{
		return true;
	}

	if(!dotnet_version.empty())
	{
		s.dotnet_version = dotnet_version;
	}

	std::string bridge_dir;
	std::string managed_dll;
	if(!locate_bridge(assembly_dir, managed_dir, bridge_dir, managed_dll))
	{
		return false;
	}

	std::string runtimeconfig = path_utils::path_join(bridge_dir, "Clrpp.Managed.runtimeconfig.json");
	if(!path_utils::path_exists(runtimeconfig))
	{
		write_default_runtimeconfig(runtimeconfig, s.dotnet_version);
	}

	auto dotnet_root = locate_dotnet_root(dotnet_root_override);
	const bool use_hostfxr =
		host == runtime_host::hostfxr || (host == runtime_host::automatic && has_hostfxr(dotnet_root));
	const auto bootstrap = use_hostfxr
							   ? start_with_hostfxr(managed_dll, runtimeconfig, dotnet_root, !dotnet_root_override.empty())
							   : start_with_coreclr(bridge_dir, runtimeconfig, dotnet_root);
	if(!bootstrap)
	{
		release_failed_start();
		return false;
	}

	// -- bootstrap the export table ----------------------------------------
	constexpr int32_t expected_count = static_cast<int32_t>(sizeof(exports) / sizeof(void*));
	static_assert(sizeof(exports) == expected_count * sizeof(void*),
				  "clrpp: exports table must be plain function pointers");

	int32_t actual_count = bootstrap(nullptr, 0);
	if(actual_count != expected_count)
	{
		log_message("clrpp: bridge export count mismatch (native " + std::to_string(expected_count) +
						" vs managed " + std::to_string(actual_count) + ")",
					"error");
		release_failed_start();
		return false;
	}

	bootstrap(reinterpret_cast<void**>(&s.table), expected_count);

	s.managed_assembly_path = managed_dll;
	s.dotnet_root = dotnet_root;
	s.host = use_hostfxr ? runtime_host::hostfxr : runtime_host::coreclr;
	s.alive = true;

	// -- wire native callbacks ----------------------------------------------
	s.table.set_log_callback(reinterpret_cast<void*>(&native_log_callback));
	s.table.internal_calls_install(reinterpret_cast<void*>(&native_icall_resolver),
								   reinterpret_cast<void*>(&native_pending_exception_query));

	log_message(std::string("clrpp: runtime initialized through ") + (use_hostfxr ? "hostfxr" : "coreclr") +
					" (bridge: " + s.managed_assembly_path + ")",
				"trace");
	return true;
}

void terminate()
{
	auto& s = state();
	if(!s.alive)
	{
		return;
	}

	s.table.gc_collect();

	s.alive = false;
	s.table = {};

	if(s.context && s.close_fn)
	{
		s.close_fn(s.context);
	}
	s.context = nullptr;

	// Intentionally keep hostfxr and coreclr loaded, and the coreclr host
	// handle: coreclr cannot be restarted within the same process, so a later
	// initialize binds the bridge on the running runtime. Unloading while
	// runtime threads are alive would crash.
}

auto managed_assembly_path() -> const std::string&
{
	return state().managed_assembly_path;
}

auto dotnet_root() -> const std::string&
{
	return state().dotnet_root;
}

auto dotnet_version() -> const std::string&
{
	return state().dotnet_version;
}

auto active_host() -> runtime_host
{
	return state().host;
}

} // namespace bridge_detail

} // namespace clr
