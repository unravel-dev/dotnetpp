#pragma once

#include "clr_config.h"

#include "clr_type_conversion_base.h"

// Forward declarations - converter specializations for containers live in
// clr_array.h / clr_list.h, mirroring the monopp layout.
namespace clr
{
template <typename T>
class clr_array;
template <typename T>
class clr_list;
} // namespace clr

#include "clr_domain.h"
#include "clr_string.h"
#include "clr_type.h"
#include "clr_type_traits.h"

namespace clr
{

template <>
struct clr_converter<clr_object>
{
	using native_type = clr_object;
	using managed_type = managed_ptr;

	static auto to_mono(const native_type& obj) -> managed_type
	{
		return obj.get_managed_ptr();
	}

	static auto from_mono(const managed_type& obj) -> native_type
	{
		if(!obj)
		{
			return {};
		}
		return native_type(obj);
	}
};

template <>
struct clr_converter<clr_type>
{
	using native_type = clr_type;
	using managed_type = managed_ptr;

	static auto to_mono(const native_type& obj) -> managed_type
	{
		// Type handles are interned; sharing hands back the same handle.
		return managed_ptr::share(obj.get_internal_ptr());
	}

	static auto from_mono(const managed_type& obj) -> native_type
	{
		if(!obj)
		{
			return {};
		}
		// Type handles arriving from user code (icall arguments, boxed
		// reflection results) are plain GCHandles owned by the caller.
		// Canonicalize to the interned session-lifetime handle so clr_type's
		// non-owning reference stays valid after the caller frees its handle.
		return native_type(bridge().intern_handle(obj.get()));
	}
};

template <>
struct clr_converter<std::string>
{
	using native_type = std::string;
	using managed_type = managed_ptr;

	static auto to_mono(const native_type& obj) -> managed_type
	{
		return managed_ptr::adopt(bridge().string_create(obj.c_str()));
	}

	static auto from_mono(const managed_type& obj) -> native_type
	{
		if(!obj)
		{
			return {};
		}
		return take_string(bridge().string_get_utf8(obj.get()));
	}

	// Internal call argument path (utf8 owned by the managed caller).
	static auto from_mono(const char* utf8) -> native_type
	{
		return utf8 ? native_type(utf8) : native_type{};
	}
};

// ---------------------------------------------------------------------------
// Variant construction for invoker arguments
// ---------------------------------------------------------------------------

/// Blittable values are passed as raw blobs.
template <typename T>
inline auto to_clr_variant(T& value) -> clr_variant
{
	static_assert(is_clr_valuetype<T>::value, "Should not pass here for non-value types");
	clr_variant v;
	v.kind = clr_variant::kind_blob;
	v.size = static_cast<int32_t>(sizeof(T));
	v.data = std::addressof(value);
	return v;
}

inline auto to_clr_variant(managed_ptr& value) -> clr_variant
{
	clr_variant v;
	if(value)
	{
		v.kind = clr_variant::kind_object_handle;
		v.data = value.get();
	}
	return v;
}

} // namespace clr
