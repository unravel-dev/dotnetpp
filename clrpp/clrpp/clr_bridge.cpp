#include "clr_bridge.h"
#include "clr_exception.h"
#include "clr_internal_call.h"
#include "clr_logger.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
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
// State
// ---------------------------------------------------------------------------

struct bridge_state
{
	bool alive = false;
	bridge_detail::exports table{};

	void* hostfxr_lib = nullptr;
	hostfxr_close_fn close_fn = nullptr;
	hostfxr_handle context = nullptr;

	std::string managed_assembly_path;
	std::string dotnet_root;
};

auto state() -> bridge_state&
{
	static bridge_state instance;
	return instance;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

#ifdef _WIN32
auto to_char_t(const std::string& utf8) -> std::wstring
{
	if(utf8.empty())
	{
		return {};
	}
	int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
	std::wstring result(static_cast<size_t>(needed > 0 ? needed - 1 : 0), L'\0');
	if(needed > 1)
	{
		MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &result[0], needed);
	}
	return result;
}
#else
auto to_char_t(const std::string& utf8) -> std::string
{
	return utf8;
}
#endif

auto load_library(const std::string& path) -> void*
{
#ifdef _WIN32
	return reinterpret_cast<void*>(LoadLibraryW(to_char_t(path).c_str()));
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

// void unload_library(void* lib)
// {
// 	if(!lib)
// 	{
// 		return;
// 	}
// #ifdef _WIN32
// 	FreeLibrary(reinterpret_cast<HMODULE>(lib));
// #else
// 	dlclose(lib);
// #endif
// }

// -- C++14 friendly path helpers (no std::filesystem) -----------------------

auto path_join(const std::string& a, const std::string& b) -> std::string
{
	if(a.empty())
	{
		return b;
	}
	const char last = a.back();
	if(last == '/' || last == '\\')
	{
		return a + b;
	}
	return a + "/" + b;
}

auto path_exists(const std::string& path) -> bool
{
#ifdef _WIN32
	return ::GetFileAttributesW(to_char_t(path).c_str()) != INVALID_FILE_ATTRIBUTES;
#else
	struct stat st
	{
	};
	return ::stat(path.c_str(), &st) == 0;
#endif
}

auto current_executable_path() -> std::string
{
#ifdef _WIN32
	char buffer[MAX_PATH]{};
	DWORD len = ::GetModuleFileNameA(nullptr, buffer, sizeof(buffer));
	return len > 0 ? std::string(buffer, len) : std::string{};
#elif defined(__APPLE__)
	return {};
#else
	char buffer[4096]{};
	ssize_t len = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
	return len > 0 ? std::string(buffer, static_cast<size_t>(len)) : std::string{};
#endif
}

auto current_directory() -> std::string
{
#ifdef _WIN32
	char buffer[MAX_PATH]{};
	DWORD len = ::GetCurrentDirectoryA(sizeof(buffer), buffer);
	return len > 0 ? std::string(buffer, len) : std::string(".");
#else
	char buffer[4096]{};
	return ::getcwd(buffer, sizeof(buffer)) ? std::string(buffer) : std::string(".");
#endif
}

auto list_subdirectories(const std::string& dir) -> std::vector<std::string>
{
	std::vector<std::string> result;
#ifdef _WIN32
	WIN32_FIND_DATAA find_data;
	auto handle = ::FindFirstFileA((dir + "\\*").c_str(), &find_data);
	if(handle == INVALID_HANDLE_VALUE)
	{
		return result;
	}
	do
	{
		if((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && find_data.cFileName[0] != '.')
		{
			result.emplace_back(find_data.cFileName);
		}
	} while(::FindNextFileA(handle, &find_data));
	::FindClose(handle);
#else
	if(auto* d = ::opendir(dir.c_str()))
	{
		while(auto* entry = ::readdir(d))
		{
			if(entry->d_name[0] == '.')
			{
				continue;
			}
			struct stat st
			{
			};
			if(::stat(path_join(dir, entry->d_name).c_str(), &st) == 0 && S_ISDIR(st.st_mode))
			{
				result.emplace_back(entry->d_name);
			}
		}
		::closedir(d);
	}
#endif
	return result;
}

auto parse_version(const std::string& text) -> std::vector<int>
{
	std::vector<int> parts;
	std::string current;
	for(char c : text)
	{
		if(c == '.')
		{
			parts.push_back(current.empty() ? 0 : std::atoi(current.c_str()));
			current.clear();
		}
		else if(c >= '0' && c <= '9')
		{
			current += c;
		}
		else
		{
			// stop at previews/rc suffixes
			break;
		}
	}
	if(!current.empty())
	{
		parts.push_back(std::atoi(current.c_str()));
	}
	return parts;
}

auto pick_highest_version_dir(const std::string& base) -> std::string
{
	std::string best;
	std::vector<int> best_version;

	for(const auto& name : list_subdirectories(base))
	{
		auto version = parse_version(name);
		if(version.empty())
		{
			continue;
		}

		if(best.empty() || std::lexicographical_compare(best_version.begin(), best_version.end(),
														version.begin(), version.end()))
		{
			best = path_join(base, name);
			best_version = version;
		}
	}

	return best;
}

auto get_env(const char* name) -> std::string
{
#ifdef _WIN32
	char buffer[4096]{};
	DWORD len = GetEnvironmentVariableA(name, buffer, sizeof(buffer));
	return len > 0 && len < sizeof(buffer) ? std::string(buffer, len) : std::string{};
#else
	const char* value = std::getenv(name);
	return value ? value : std::string{};
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

void write_default_runtimeconfig(const std::string& path)
{
	std::ofstream file(path.c_str());
	file << R"({
  "runtimeOptions": {
    "tfm": "net8.0",
    "rollForward": "LatestMajor",
    "framework": {
      "name": "Microsoft.NETCore.App",
      "version": "8.0.0"
    }
  }
})";
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

auto locate_dotnet_root(const std::string& override_root) -> std::string
{
	if(!override_root.empty())
	{
		return override_root;
	}

	auto env_root = get_env("DOTNET_ROOT");
	if(!env_root.empty())
	{
		return env_root;
	}

#ifdef _WIN32
	return "C:/Program Files/dotnet";
#elif defined(__APPLE__)
	return "/usr/local/share/dotnet";
#else
	if(path_exists("/usr/share/dotnet"))
	{
		return "/usr/share/dotnet";
	}
	return "/usr/lib/dotnet";
#endif
}

auto initialize(const std::string& assembly_dir, const std::string& dotnet_root_override) -> bool
{
	auto& s = state();
	if(s.alive)
	{
		return true;
	}

	// -- locate the managed bridge -----------------------------------------
	// The bridge and its dependencies (Mono.Cecil, Microsoft.Diagnostics.*)
	// live together in one directory; the bridge resolves its dependencies
	// from its own location. Probe, in order: the explicit assembly_dir, a
	// clrpp/ subfolder next to the executable, the executable directory
	// itself, then the same two relative to the working directory.
	std::vector<std::string> candidate_dirs;
	if(!assembly_dir.empty())
	{
		candidate_dirs.push_back(assembly_dir);
		candidate_dirs.push_back(path_join(assembly_dir, "clrpp"));
	}
	auto exe_path = current_executable_path();
	if(!exe_path.empty())
	{
		auto slash = exe_path.find_last_of("/\\");
		if(slash != std::string::npos)
		{
			auto exe_dir = exe_path.substr(0, slash);
			candidate_dirs.push_back(path_join(exe_dir, "clrpp"));
			candidate_dirs.push_back(exe_dir);
		}
	}
	candidate_dirs.push_back(path_join(current_directory(), "clrpp"));
	candidate_dirs.push_back(current_directory());

	std::string managed_dir;
	std::string managed_dll;
	for(const auto& dir : candidate_dirs)
	{
		auto candidate = path_join(dir, "Clrpp.Managed.dll");
		if(path_exists(candidate))
		{
			managed_dir = dir;
			managed_dll = candidate;
			break;
		}
	}

	if(managed_dll.empty())
	{
		std::string tried;
		for(const auto& dir : candidate_dirs)
		{
			tried += (tried.empty() ? "" : ", ") + dir;
		}
		log_message("clrpp: Clrpp.Managed.dll not found; searched: " + tried, "error");
		return false;
	}

	std::string runtimeconfig = path_join(managed_dir, "Clrpp.Managed.runtimeconfig.json");
	if(!path_exists(runtimeconfig))
	{
		write_default_runtimeconfig(runtimeconfig);
	}

	// -- locate and load hostfxr -------------------------------------------
	auto dotnet_root = locate_dotnet_root(dotnet_root_override);
	auto fxr_base = path_join(path_join(dotnet_root, "host"), "fxr");
	auto fxr_dir = pick_highest_version_dir(fxr_base);
	if(fxr_dir.empty())
	{
		log_message("clrpp: hostfxr not found under " + fxr_base, "error");
		return false;
	}

	auto hostfxr_path = path_join(fxr_dir, hostfxr_library_name());
	s.hostfxr_lib = load_library(hostfxr_path);
	if(!s.hostfxr_lib)
	{
		log_message("clrpp: failed to load " + hostfxr_path, "error");
		return false;
	}

	auto init_fn = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
		get_symbol(s.hostfxr_lib, "hostfxr_initialize_for_runtime_config"));
	auto get_delegate_fn = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
		get_symbol(s.hostfxr_lib, "hostfxr_get_runtime_delegate"));
	s.close_fn = reinterpret_cast<hostfxr_close_fn>(get_symbol(s.hostfxr_lib, "hostfxr_close"));

	if(!init_fn || !get_delegate_fn || !s.close_fn)
	{
		log_message("clrpp: hostfxr exports missing in " + hostfxr_path, "error");
		return false;
	}

	// -- initialize the runtime --------------------------------------------
	auto config_path = to_char_t(runtimeconfig);

	// Only pass explicit parameters when the caller overrides the dotnet
	// root; otherwise let hostfxr use its own detection. Note: the root must
	// use native separators - forward slashes leak into the runtime's
	// assembly paths and make coreclr initialization fail with E_INVALIDARG.
	std::string normalized_root = dotnet_root;
#ifdef _WIN32
	std::replace(normalized_root.begin(), normalized_root.end(), '/', '\\');
#endif
	auto root_path = to_char_t(normalized_root);
	auto host_path = to_char_t(current_executable_path());

	hostfxr_initialize_parameters params{};
	params.size = sizeof(params);
	params.host_path = host_path.empty() ? nullptr : host_path.c_str();
	params.dotnet_root = root_path.c_str();

	const bool use_params = !dotnet_root_override.empty();

	int32_t rc = init_fn(config_path.c_str(), use_params ? &params : nullptr, &s.context);
	// 0 = success, 1 = success_host_already_initialized, 2 = success_different_runtime_properties
	if(rc < 0 || rc > 2 || s.context == nullptr)
	{
		log_message("clrpp: hostfxr_initialize_for_runtime_config failed with 0x" +
						std::to_string(rc),
					"error");
		return false;
	}

	void* load_assembly_ptr = nullptr;
	rc = get_delegate_fn(s.context, hdt_load_assembly_and_get_function_pointer, &load_assembly_ptr);
	if(rc != 0 || !load_assembly_ptr)
	{
		log_message("clrpp: failed to acquire load_assembly_and_get_function_pointer", "error");
		return false;
	}

	auto load_assembly_and_get_fn =
		reinterpret_cast<load_assembly_and_get_function_pointer_fn>(load_assembly_ptr);

	// -- bootstrap the export table ----------------------------------------
	using bootstrap_fn = int32_t(CLRPP_CALLTYPE*)(void**, int32_t);
	bootstrap_fn bootstrap = nullptr;

	auto dll_path = to_char_t(managed_dll);
	auto type_name = to_char_t("Clrpp.Bridge, Clrpp.Managed");
	auto method_name = to_char_t("Bootstrap");

	rc = load_assembly_and_get_fn(dll_path.c_str(), type_name.c_str(), method_name.c_str(),
								  UNMANAGEDCALLERSONLY_METHOD, nullptr,
								  reinterpret_cast<void**>(&bootstrap));
	if(rc != 0 || !bootstrap)
	{
		log_message("clrpp: failed to bind Clrpp.Bridge.Bootstrap (hr=" + std::to_string(rc) + ")",
					"error");
		return false;
	}

	constexpr int32_t expected_count = static_cast<int32_t>(sizeof(exports) / sizeof(void*));
	static_assert(sizeof(exports) == expected_count * sizeof(void*),
				  "clrpp: exports table must be plain function pointers");

	int32_t actual_count = bootstrap(nullptr, 0);
	if(actual_count != expected_count)
	{
		log_message("clrpp: bridge export count mismatch (native " + std::to_string(expected_count) +
						" vs managed " + std::to_string(actual_count) + ")",
					"error");
		return false;
	}

	bootstrap(reinterpret_cast<void**>(&s.table), expected_count);

	s.managed_assembly_path = managed_dll;
	s.dotnet_root = dotnet_root;
	s.alive = true;

	// -- wire native callbacks ----------------------------------------------
	s.table.set_log_callback(reinterpret_cast<void*>(&native_log_callback));
	s.table.internal_calls_install(reinterpret_cast<void*>(&native_icall_resolver),
								   reinterpret_cast<void*>(&native_pending_exception_query));
	s.table.set_internal_call_weaving(get_internal_call_weaving() ? 1 : 0);

	log_message("clrpp: runtime initialized (bridge: " + s.managed_assembly_path + ")", "trace");
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

	// Intentionally keep hostfxr loaded: coreclr cannot be restarted within
	// the same process anyway, and unloading while runtime threads are alive
	// would crash.
}

auto managed_assembly_path() -> const std::string&
{
	return state().managed_assembly_path;
}

auto dotnet_root() -> const std::string&
{
	return state().dotnet_root;
}

} // namespace bridge_detail

} // namespace clr
