#include "mono_arg.h"
#include "mono_object.h"

namespace mono
{

auto to_managed_arg(MonoObject* value_obj, const mono_type& type) -> void*
{
	if (!type.valid())
	{
		// No type info - pass MonoObject* directly
		return value_obj;
	}

	if (!type.is_valuetype())
	{
		// Reference parameter - pass MonoObject* directly
		return value_obj;
	}
	else
	{	
		if (!value_obj)
		{
			return nullptr;
		}

		auto vklass = mono_object_get_class(value_obj);
		
		// Check if value is a boxed value type
		if (!mono_class_is_valuetype(vklass))
		{
			return nullptr; // Can't unbox a reference type
		}

		// Check type compatibility
		if (vklass != type.get_internal_ptr())
		{
			return nullptr; // Type mismatch
		}

		// Value type parameter - unbox
		return mono_object_unbox(value_obj);
	}
}

auto to_managed_arg(const mono_object& value, const mono_type& type) -> void*
{	
	MonoObject* value_obj = value.get_internal_ptr();
	return to_managed_arg(value_obj, type);
}

} // namespace mono
