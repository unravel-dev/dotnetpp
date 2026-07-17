#pragma once

#include "mono_config.h"

#include "mono_type_conversion_base.h"
// Forward declarations - converter specializations moved to respective headers to break circular dependency
namespace mono { 
	template <typename T> class mono_array; 
	template <typename T> class mono_list; 
}
#include "mono_domain.h"
#include "mono_string.h"
#include "mono_type.h"
#include "mono_type_traits.h"
#include "mono_arg.h"

namespace mono
{



template <>
struct mono_converter<mono_object>
{
	using native_type = mono_object;
	using managed_type = MonoObject*;

	static auto to_managed(const native_type& obj) -> managed_type
	{
		return obj.get_internal_ptr();
	}

	static auto from_managed(const managed_type& obj) -> native_type
	{
		if(!obj)
		{
			return {};
		}
		return native_type(obj);
	}
};

template <>
struct mono_converter<mono_type>
{
	using native_type = mono_type;
	using managed_type = MonoReflectionType*;

	static auto to_managed(const native_type& obj) -> managed_type
	{
		// Get the current Mono domain
		MonoDomain* domain = mono_domain_get();

		// Obtain the MonoType* from your native_type (assuming a getter function)
		MonoType* monoType = mono_class_get_type(obj.get_internal_ptr());

		// Convert MonoType* to MonoReflectionType*
		MonoReflectionType* reflectionType = mono_type_get_object(domain, monoType);

		return reflectionType;
	}

	static auto from_managed(const managed_type& obj) -> native_type
	{
		if(!obj)
		{
			return {};
		}
		MonoType* monoType = mono_reflection_type_get_type(obj);
		return native_type(monoType);
	}
};

template <>
struct mono_converter<std::string>
{
	using native_type = std::string;
	using managed_type = MonoObject*; 

	static auto to_managed(const native_type& obj) -> managed_type
	{
		const auto& domain = mono_domain::get_current_domain();
		return mono_string(domain, obj).get_internal_ptr();
	}

	static auto from_managed(const managed_type& obj) -> native_type
	{
		if(!obj)
		{
			return {};
		}
		return mono_string(mono_object(obj)).as_utf8();
	}
};

// mono_converter specializations for std::vector<T> and mono_array<T> 
// moved to mono_array.h to break circular dependency with mono_type_conversion.h
// mono_converter specializations for std::list<T> and mono_list<T> 
// moved to mono_list.h to break circular dependency with mono_type_conversion.h


} // namespace mono
