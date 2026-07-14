#include "clr_internal_call.h"
#include "clr_logger.h"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>

#ifdef _WIN32
#include <objbase.h>
#endif

namespace clr
{

namespace
{

auto get_registry() -> std::unordered_map<std::string, void*>&
{
	static std::unordered_map<std::string, void*> registry;
	return registry;
}

auto get_registry_mutex() -> std::mutex&
{
	static std::mutex m;
	return m;
}

} // namespace

auto weave_assembly(const std::string& assembly_path) -> bool
{
	if(!bridge_alive())
	{
		log_message("clrpp: weave_assembly requires an initialized runtime (" + assembly_path + ")",
					"error");
		return false;
	}

	return bridge().weave_assembly(assembly_path.c_str()) >= 0;
}

void add_internal_call(const std::string& name, void* func)
{
	std::lock_guard<std::mutex> lock(get_registry_mutex());
	get_registry()[name] = func;
}

auto find_internal_call(const std::string& name) -> void*
{
	std::lock_guard<std::mutex> lock(get_registry_mutex());
	auto& registry = get_registry();
	auto it = registry.find(name);
	return it != registry.end() ? it->second : nullptr;
}

auto alloc_interop_string(const std::string& value) -> const char*
{
	// Managed side frees with Marshal.FreeCoTaskMem, which maps to
	// CoTaskMemFree on windows and free() elsewhere.
	const auto size = value.size() + 1;
#ifdef _WIN32
	auto* buffer = static_cast<char*>(::CoTaskMemAlloc(size));
#else
	auto* buffer = static_cast<char*>(std::malloc(size));
#endif
	if(buffer)
	{
		std::memcpy(buffer, value.c_str(), size);
	}
	return buffer;
}

namespace detail
{

auto duplicate_handle_for_transfer(const managed_ptr& owned) -> clr_handle
{
	if(!owned)
	{
		return nullptr;
	}
	return bridge().duplicate_handle(owned.get());
}

} // namespace detail

} // namespace clr
