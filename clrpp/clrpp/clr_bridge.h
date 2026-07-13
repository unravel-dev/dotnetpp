#pragma once

#include "clr_config.h"

#include <functional>

/*
 * Native side of the Clrpp.Managed bridge.
 *
 * The CoreCLR runtime is hosted through hostfxr. A single managed entry point
 * (Clrpp.Bridge.Bootstrap) uploads a table of UnmanagedCallersOnly function
 * pointers which the rest of clrpp calls through.
 */

// Calling convention of UnmanagedCallersOnly exports (platform default).
#if defined(_WIN32) && !defined(_WIN64)
#define CLRPP_CALLTYPE __stdcall
#else
#define CLRPP_CALLTYPE
#endif

namespace clr
{

/// Raw GCHandle as IntPtr. Zero means null.
using clr_handle = void*;

/// Layout must match Clrpp.NativeVariant.
struct clr_variant
{
	enum kind_t : int32_t
	{
		kind_empty = 0,
		kind_blob = 1,
		kind_string_utf8 = 2,
		kind_object_handle = 3
	};

	int32_t kind = kind_empty;
	int32_t size = 0;
	void* data = nullptr;
};

/// Layout must match Clrpp.NativeExceptionInfo.
struct clr_exception_info_raw
{
	const char* type_name = nullptr;
	const char* message = nullptr;
	const char* source = nullptr;
	const char* stack_trace = nullptr;
	int32_t has_value = 0;
};

namespace bridge_detail
{

// Must match the export order in Clrpp.Managed Bridge.Bootstrap.cs.
struct exports
{
	// core
	void(CLRPP_CALLTYPE* free_string)(const char*);
	void(CLRPP_CALLTYPE* free_handle)(clr_handle);
	clr_handle(CLRPP_CALLTYPE* duplicate_handle)(clr_handle);
	int32_t(CLRPP_CALLTYPE* handle_equals)(clr_handle, clr_handle);
	void(CLRPP_CALLTYPE* gc_collect)();
	int64_t(CLRPP_CALLTYPE* gc_get_heap_size)();
	int64_t(CLRPP_CALLTYPE* gc_get_used_size)();
	void(CLRPP_CALLTYPE* set_log_callback)(void*);
	void(CLRPP_CALLTYPE* internal_calls_install)(void*, void*);

	// runtime
	clr_handle(CLRPP_CALLTYPE* domain_create)(const char*);
	/// Returns 0 when the context was verifiably collected, 1 when it leaked
	/// (details are logged managed-side), -1 on error.
	int32_t(CLRPP_CALLTYPE* domain_unload)(clr_handle);
	const char*(CLRPP_CALLTYPE* domain_get_name)(clr_handle);
	void(CLRPP_CALLTYPE* domain_add_search_path)(clr_handle, const char*);
	clr_handle(CLRPP_CALLTYPE* assembly_load)(clr_handle, const char*, clr_exception_info_raw*);
	clr_handle(CLRPP_CALLTYPE* assembly_get_corlib)();
	const char*(CLRPP_CALLTYPE* assembly_get_name)(clr_handle);
	const char*(CLRPP_CALLTYPE* assembly_dump_references)(clr_handle);
	clr_handle(CLRPP_CALLTYPE* assembly_get_type)(clr_handle, const char*);
	int32_t(CLRPP_CALLTYPE* assembly_get_types)(clr_handle, clr_handle*, int32_t);
	int32_t(CLRPP_CALLTYPE* assembly_get_types_derived_from)(clr_handle, clr_handle, clr_handle*, int32_t);

	// type
	const char*(CLRPP_CALLTYPE* type_get_name)(clr_handle);
	const char*(CLRPP_CALLTYPE* type_get_namespace)(clr_handle);
	const char*(CLRPP_CALLTYPE* type_get_fullname)(clr_handle);
	int32_t(CLRPP_CALLTYPE* type_get_flags)(clr_handle);
	clr_handle(CLRPP_CALLTYPE* type_get_base_type)(clr_handle);
	clr_handle(CLRPP_CALLTYPE* type_get_nesting_type)(clr_handle);
	int32_t(CLRPP_CALLTYPE* type_get_nested_types)(clr_handle, clr_handle*, int32_t);
	int32_t(CLRPP_CALLTYPE* type_is_derived_from)(clr_handle, clr_handle);
	clr_handle(CLRPP_CALLTYPE* type_get_enum_base_type)(clr_handle);
	int32_t(CLRPP_CALLTYPE* type_get_enum_values)(clr_handle, int64_t*, const char**, int32_t);
	int32_t(CLRPP_CALLTYPE* type_get_rank)(clr_handle);
	clr_handle(CLRPP_CALLTYPE* type_get_element_type)(clr_handle);
	int32_t(CLRPP_CALLTYPE* type_get_sizeof)(clr_handle);
	int32_t(CLRPP_CALLTYPE* type_get_alignof)(clr_handle);
	clr_handle(CLRPP_CALLTYPE* type_get_list_type)(clr_handle);
	clr_handle(CLRPP_CALLTYPE* type_get_method)(clr_handle, const char*, int32_t);
	clr_handle(CLRPP_CALLTYPE* type_get_method_by_signature)(clr_handle, const char*);
	int32_t(CLRPP_CALLTYPE* type_get_methods)(clr_handle, int32_t, clr_handle*, int32_t);
	clr_handle(CLRPP_CALLTYPE* type_get_field)(clr_handle, const char*);
	int32_t(CLRPP_CALLTYPE* type_get_fields)(clr_handle, int32_t, clr_handle*, int32_t);
	clr_handle(CLRPP_CALLTYPE* type_get_property)(clr_handle, const char*);
	int32_t(CLRPP_CALLTYPE* type_get_properties)(clr_handle, int32_t, clr_handle*, int32_t);
	int32_t(CLRPP_CALLTYPE* type_get_attributes)(clr_handle, int32_t, clr_handle*, int32_t);

	// method
	const char*(CLRPP_CALLTYPE* method_get_name)(clr_handle);
	const char*(CLRPP_CALLTYPE* method_get_fullname)(clr_handle);
	int32_t(CLRPP_CALLTYPE* method_get_flags)(clr_handle);
	clr_handle(CLRPP_CALLTYPE* method_get_return_type)(clr_handle);
	int32_t(CLRPP_CALLTYPE* method_get_param_types)(clr_handle, clr_handle*, int32_t);
	int32_t(CLRPP_CALLTYPE* method_get_attributes)(clr_handle, clr_handle*, int32_t);
	clr_handle(CLRPP_CALLTYPE* method_get_declaring_type)(clr_handle);

	// field
	const char*(CLRPP_CALLTYPE* field_get_name)(clr_handle);
	clr_handle(CLRPP_CALLTYPE* field_get_type)(clr_handle);
	int32_t(CLRPP_CALLTYPE* field_get_flags)(clr_handle);
	clr_handle(CLRPP_CALLTYPE* field_get_declaring_type)(clr_handle);
	int32_t(CLRPP_CALLTYPE* field_get_attributes)(clr_handle, clr_handle*, int32_t);

	// property
	const char*(CLRPP_CALLTYPE* property_get_name)(clr_handle);
	clr_handle(CLRPP_CALLTYPE* property_get_type)(clr_handle);
	clr_handle(CLRPP_CALLTYPE* property_get_get_method)(clr_handle);
	clr_handle(CLRPP_CALLTYPE* property_get_set_method)(clr_handle);
	clr_handle(CLRPP_CALLTYPE* property_get_declaring_type)(clr_handle);
	int32_t(CLRPP_CALLTYPE* property_get_flags)(clr_handle);
	int32_t(CLRPP_CALLTYPE* property_get_attributes)(clr_handle, clr_handle*, int32_t);

	// invoke
	clr_handle(CLRPP_CALLTYPE* object_create)(clr_handle, clr_exception_info_raw*);
	clr_handle(CLRPP_CALLTYPE* object_get_type)(clr_handle);
	clr_handle(CLRPP_CALLTYPE* object_box)(clr_handle, const void*, int32_t);
	int32_t(CLRPP_CALLTYPE* object_unbox)(clr_handle, void*, int32_t);
	clr_handle(CLRPP_CALLTYPE* string_create)(const char*);
	const char*(CLRPP_CALLTYPE* string_get_utf8)(clr_handle);
	void(CLRPP_CALLTYPE* method_invoke)(clr_handle, clr_handle, clr_variant*, int32_t, clr_variant*,
										clr_exception_info_raw*);
	void(CLRPP_CALLTYPE* field_get_value)(clr_handle, clr_handle, clr_variant*, clr_exception_info_raw*);
	void(CLRPP_CALLTYPE* field_set_value)(clr_handle, clr_handle, clr_variant*, clr_exception_info_raw*);
	clr_handle(CLRPP_CALLTYPE* array_create)(clr_handle, int64_t);
	int64_t(CLRPP_CALLTYPE* array_get_length)(clr_handle);
	void(CLRPP_CALLTYPE* array_get_element)(clr_handle, int64_t, clr_variant*, clr_exception_info_raw*);
	void(CLRPP_CALLTYPE* array_set_element)(clr_handle, int64_t, clr_variant*, clr_exception_info_raw*);
	int64_t(CLRPP_CALLTYPE* array_copy_to)(clr_handle, int64_t, void*, int64_t);
	int64_t(CLRPP_CALLTYPE* array_copy_from)(clr_handle, int64_t, const void*, int64_t);

	// appended
	void(CLRPP_CALLTYPE* set_internal_call_weaving)(int32_t);
	clr_handle(CLRPP_CALLTYPE* intern_handle)(clr_handle);
	int32_t(CLRPP_CALLTYPE* is_debugger_attached)();
};

// Runtime lifecycle, used by clr::init / clr::shutdown. managed_dir is the
// bridge subfolder name probed next to assembly_dir/exe/cwd; empty means the
// compile-time default (CLRPP_MANAGED_DIR). dotnet_version (major.minor,
// e.g. "10.0") is used for the fallback runtimeconfig and exposed to tooling;
// empty means the compile-time default.
auto initialize(const std::string& assembly_dir,
				const std::string& dotnet_root_override,
				const std::string& managed_dir = {},
				const std::string& dotnet_version = {}) -> bool;
void terminate();
auto managed_assembly_path() -> const std::string&;
auto dotnet_root() -> const std::string&;
auto dotnet_version() -> const std::string&;

} // namespace bridge_detail

/// Access the export table. Throws clr_exception if the runtime is not initialized.
auto bridge() -> const bridge_detail::exports&;

/// True between successful init() and shutdown().
auto bridge_alive() -> bool;

/// Consume a bridge-allocated utf8 string (frees it managed-side).
auto take_string(const char* str) -> std::string;

/*
 * Shared RAII wrapper over a GCHandle. Interned handles (types, methods,
 * fields, properties, assemblies) survive FreeHandle so sharing them here is
 * harmless; instance handles are freed when the last reference drops.
 */
class managed_ptr
{
public:
	managed_ptr() = default;

	/// Take ownership of a raw handle returned by the bridge.
	static auto adopt(clr_handle raw) -> managed_ptr;

	/// Share an existing handle (duplicates it managed-side).
	static auto share(clr_handle raw) -> managed_ptr;

	auto get() const -> clr_handle
	{
		return handle_ ? handle_.get() : nullptr;
	}

	explicit operator bool() const
	{
		return get() != nullptr;
	}

private:
	std::shared_ptr<void> handle_;
};

} // namespace clr
