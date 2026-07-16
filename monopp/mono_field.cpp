#include "mono_field.h"
#include "mono_domain.h"
#include "mono_exception.h"
#include "mono_object.h"

#include <unordered_map>

BEGIN_MONO_INCLUDE
#include <mono/metadata/appdomain.h>
#include <mono/metadata/attrdefs.h>
#include <mono/metadata/debug-helpers.h>
END_MONO_INCLUDE
namespace mono
{
struct mono_field::meta_info
{
	std::string name;
	std::string fullname;
	std::string full_declname;
};

namespace
{

auto get_field_cache() -> std::unordered_map<MonoClassField*, std::shared_ptr<mono_field::meta_info>>&
{
	static std::unordered_map<MonoClassField*, std::shared_ptr<mono_field::meta_info>> field_cache;
	return field_cache;
}

auto get_meta_info(MonoClassField* field) -> std::shared_ptr<mono_field::meta_info>
{
	auto& cache = get_field_cache();
	auto it = cache.find(field);
	if(it != cache.end())
	{
		return it->second;
	}
	return nullptr;
}

void set_meta_info(MonoClassField* field, std::shared_ptr<mono_field::meta_info> meta)
{
	auto& cache = get_field_cache();
	cache[field] = meta;
}

} // namespace

mono_field::mono_field(const mono_type& type, const std::string& name)
{
	auto check_type = type;
	while(!field_ && check_type.valid())
	{
		field_ = mono_class_get_field_from_name(check_type.get_internal_ptr(), name.c_str());
		check_type = check_type.get_base_type();

	}

	if(!field_)
	{
		throw mono_exception("NATIVE::Could not get field : " + name + " for class " + type.get_name());
	}
	const auto& domain = mono_domain::get_current_domain();

	if(is_static())
	{
		// Get the class that actually owns this field (may be different from type if field is inherited)
		MonoClass* owning_class = mono_field_get_parent(field_);
		owning_type_vtable_ = mono_class_vtable(domain.get_internal_ptr(), owning_class);
		// Initialize the class to ensure static fields are initialized
		mono_runtime_class_init(owning_type_vtable_);
	}

	auto field_type = mono_field_get_type(field_);
	type_ = mono_type(mono_class_from_mono_type(field_type));

	generate_meta();
}

void mono_field::generate_meta()
{
	auto meta = get_meta_info(field_);
	if(!meta)
	{
		meta = std::make_shared<meta_info>();
		meta->name = get_name();
		meta->fullname = get_fullname();
		meta->full_declname = get_full_declname();
		set_meta_info(field_, meta);
	}

	meta_ = meta;
}

auto mono_field::is_valuetype() const -> bool
{
	return get_type().is_valuetype();
}

auto mono_field::get_name() const -> std::string
{
	if(meta_)
	{
		return meta_->name;
	}
	return mono_field_get_name(field_);
}
auto mono_field::get_fullname() const -> std::string
{
	if(meta_)
	{
		return meta_->fullname;
	}
	char* mono_name = mono_field_full_name(field_);
	std::string name(mono_name);
	mono_free(mono_name);
	return name;
}

auto mono_field::get_full_declname() const -> std::string
{
	if(meta_)
	{
		return meta_->full_declname;
	}
	std::string storage = (is_static() ? " static " : " ");
	return to_string(get_visibility()) + storage + get_fullname();
}
auto mono_field::get_type() const -> const mono_type&
{
	return type_;
}

auto mono_field::get_visibility() const -> visibility
{
	uint32_t flags = mono_field_get_flags(field_) & MONO_FIELD_ATTR_FIELD_ACCESS_MASK;

	if(flags == MONO_FIELD_ATTR_PRIVATE)
		return visibility::vis_private;
	else if(flags == MONO_FIELD_ATTR_FAM_AND_ASSEM)
		return visibility::vis_protected_internal;
	else if(flags == MONO_FIELD_ATTR_ASSEMBLY)
		return visibility::vis_internal;
	else if(flags == MONO_FIELD_ATTR_FAMILY)
		return visibility::vis_protected;
	else if(flags == MONO_FIELD_ATTR_PUBLIC)
		return visibility::vis_public;

	assert(false);

	return visibility::vis_private;
}

auto mono_field::is_static() const -> bool
{
	uint32_t flags = mono_field_get_flags(field_);

	return (flags & MONO_FIELD_ATTR_STATIC) != 0;
}

auto mono_field::get_attributes() const -> std::vector<mono_object>
{
	std::vector<mono_object> result;

	auto parent_class = mono_field_get_parent(field_);
	// Get custom attributes from the field
	MonoCustomAttrInfo* attr_info = mono_custom_attrs_from_field(parent_class, field_);

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

auto mono_field::is_readonly() const -> bool
{
	uint32_t flags = mono_field_get_flags(field_);
	return (flags & MONO_FIELD_ATTR_INIT_ONLY) != 0;
}

auto mono_field::is_const() const -> bool
{
	uint32_t flags = mono_field_get_flags(field_);
	return (flags & MONO_FIELD_ATTR_LITERAL) != 0;
}

auto mono_field::has_attribute_fullname(const std::string& attribute_full_name) const -> bool
{
	return get_attribute_fullname(attribute_full_name).valid();
}

auto mono_field::has_attribute(const std::string& attribute_name) const -> bool
{
	return get_attribute(attribute_name).valid();
}

auto mono_field::get_attribute(const std::string& attribute_name) const -> mono_object
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

auto mono_field::get_attribute_fullname(const std::string& attribute_full_name) const -> mono_object
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

auto mono_field::is_backing_field() const -> bool
{
	// 2) auto-property backing fields are tagged [CompilerGenerated]
	if(has_attribute_fullname("System.Runtime.CompilerServices.CompilerGeneratedAttribute"))
		return true;

	auto name = get_name();
	// 3) they also always have the exact pattern <X>k__BackingField
	//    you can optionally double-check:
	if(name.size() > 0 && name.front() == '<' && name.find(">k__BackingField") != std::string::npos)
		return true;

	return false;
}

auto mono_field::equals(const mono_field& other) const -> bool
{
	return get_internal_ptr() == other.get_internal_ptr();
}

auto mono_field::get_internal_ptr() const -> MonoClassField*
{
	return field_;
}

void reset_field_cache()
{
	auto& cache = get_field_cache();
	cache.clear();
}
} // namespace mono
