#pragma once

#include "mono_config.h"

#include "mono_type.h"
#include "mono_type_traits.h"

namespace mono
{

// Forward declarations
class mono_object;

// Basic to_managed_arg for value types with type info
template <typename T>
inline auto to_managed_arg(T& t, const mono_type& type) -> void*
{
	static_assert(is_mono_valuetype<T>::value, "Should not pass here for non-pod types");
	(void)type; // Suppress unused parameter warning
	return std::addressof(t);
}

// Basic to_managed_arg for MonoObject*
auto to_managed_arg(MonoObject* value_obj, const mono_type& type) -> void*;

// Handle mono_object wrapper
auto to_managed_arg(const mono_object& value, const mono_type& type) -> void*;


} // namespace mono
