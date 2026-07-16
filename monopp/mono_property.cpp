#include "mono_property.h"
#include "mono_exception.h"
#include "mono_method.h"
#include "mono_object.h"

#include <unordered_map>

BEGIN_MONO_INCLUDE
#include <mono/metadata/appdomain.h>
#include <mono/metadata/attrdefs.h>
#include <mono/metadata/debug-helpers.h>
END_MONO_INCLUDE

namespace mono
{
struct mono_property::meta_info
{
	std::string name;
	std::string fullname;
	std::string full_declname;
};

namespace
{

auto get_property_cache() -> std::unordered_map<MonoProperty*, std::shared_ptr<mono_property::meta_info>>&
{
	static std::unordered_map<MonoProperty*, std::shared_ptr<mono_property::meta_info>> property_cache;
	return property_cache;
}

auto get_meta_info(MonoProperty* property) -> std::shared_ptr<mono_property::meta_info>
{
	auto& cache = get_property_cache();
	auto it = cache.find(property);
	if(it != cache.end())
	{
		return it->second;
	}
	return nullptr;
}

void set_meta_info(MonoProperty* property, std::shared_ptr<mono_property::meta_info> meta)
{
	auto& cache = get_property_cache();
	cache[property] = meta;
}

} // namespace

mono_property::mono_property(const mono_type& type, const std::string& name)
{
	auto check_type = type;
	while(!property_ && check_type.valid())
	{
		property_ = mono_class_get_property_from_name(check_type.get_internal_ptr(), name.c_str());
		check_type = check_type.get_base_type();
	}
	if(!property_)
	{
		throw mono_exception("NATIVE::Could not get property : " + name + " for class " + type.get_name());
	}
	
	auto get_method = get_get_method();
	type_ = get_method.get_return_type();

	generate_meta();
}

auto mono_property::is_valid() const -> bool
{
	return property_ != nullptr;
}

auto mono_property::equals(const mono_property& other) const -> bool
{
	return get_internal_ptr() == other.get_internal_ptr();
}

auto mono_property::get_internal_ptr() const -> MonoProperty*
{
	return property_;
}

auto mono_property::get_name() const -> std::string
{
	if(meta_)
	{
		return meta_->name;
	}
	return mono_property_get_name(get_internal_ptr());
}

auto mono_property::get_fullname() const -> std::string
{
	if(meta_)
	{
		return meta_->fullname;
	}
	return mono_property_get_name(get_internal_ptr());
}

auto mono_property::get_full_declname() const -> std::string
{
	if(meta_)
	{
		return meta_->full_declname;
	}
	std::string storage = (is_static() ? " static " : " ");
	return to_string(get_visibility()) + storage + get_name();
}

auto mono_property::get_type() const -> const mono_type&
{
	return type_;
}

auto mono_property::get_get_method() const -> mono_method
{
	auto method = mono_property_get_get_method(property_);
	return mono_method(method);
}
auto mono_property::get_set_method() const -> mono_method
{
	auto method = mono_property_get_set_method(property_);
	return mono_method(method);
}

auto mono_property::get_visibility() const -> visibility
{
	auto getter_vis = visibility::vis_public;
	try
	{
		auto getter = get_get_method();
		getter_vis = getter.get_visibility();
	}
	catch(const mono_exception&)
	{
	}
	auto setter_vis = visibility::vis_public;
	try
	{
		auto setter = get_set_method();
		if(setter)
		{
			setter_vis = setter.get_visibility();
		}
	}
	catch(const mono_exception&)
	{
	}
	if(int(getter_vis) < int(setter_vis))
	{
		return getter_vis;
	}

	return setter_vis;
}

auto mono_property::is_static() const -> bool
{
	auto getter = get_get_method();
	return getter.is_static();
}

auto mono_property::is_readonly() const -> bool
{
	auto getter = get_set_method();
	return !getter.valid();
}


void mono_property::generate_meta()
{
	auto meta = get_meta_info(property_);
	if(!meta)
	{
		meta = std::make_shared<meta_info>();
		meta->name = get_name();
		meta->fullname = get_fullname();
		meta->full_declname = get_full_declname();
		set_meta_info(property_, meta);
	}

	meta_ = meta;
}

auto mono_property::get_attributes() const -> std::vector<mono_object>
{
	std::vector<mono_object> result;

	auto parent_class = mono_property_get_parent(property_);

	// Get custom attributes from the property
	MonoCustomAttrInfo* attr_info = mono_custom_attrs_from_property(parent_class, property_);

	if(attr_info)
	{
		result.reserve(attr_info->num_attrs);

		// Iterate over the custom attributes
		for(int i = 0; i < attr_info->num_attrs; ++i)
		{
			MonoCustomAttrEntry* entry = &attr_info->attrs[i];

			// Get the MonoClass* of the attribute
			MonoClass* attr_class = mono_method_get_class(entry->ctor);

			if(attr_class)
			{
				MonoObject* attr_obj = mono_custom_attrs_get_attr(attr_info, attr_class);
				// Add the attribute instance to the result vector
				if(attr_obj)
				{
					result.emplace_back(attr_obj);
				}
			}
		}
		// Free the attribute info when done
		mono_custom_attrs_free(attr_info);
	}

	return result;
}

auto mono_property::has_attribute_fullname(const std::string& attribute_full_name) const -> bool
{
	return get_attribute_fullname(attribute_full_name).valid();
}

auto mono_property::has_attribute(const std::string& attribute_name) const -> bool
{
	return get_attribute(attribute_name).valid();
}

auto mono_property::get_attribute(const std::string& attribute_name) const -> mono_object
{
	for (auto& attr_obj : get_attributes())
	{
		if(attribute_name == attr_obj.get_type().get_name())
		{
			return attr_obj;
		}
	}
	return mono_object();
}

auto mono_property::get_attribute_fullname(const std::string& attribute_full_name) const -> mono_object
{
	for (auto& attr_obj : get_attributes())
	{
		if(attribute_full_name == attr_obj.get_type().get_fullname())
		{
			return attr_obj;
		}
	}
	return mono_object();
}
auto mono_property::is_special_name() const -> bool
{
	uint32_t flags = mono_property_get_flags(property_);
	return (flags & MONO_PROPERTY_ATTR_SPECIAL_NAME) != 0;
}

auto mono_property::has_default() const -> bool
{
	uint32_t flags = mono_property_get_flags(property_);
	return (flags & MONO_PROPERTY_ATTR_HAS_DEFAULT) != 0;
}
void reset_property_cache()
{
	auto& cache = get_property_cache();
	cache.clear();
}
} // namespace mono
