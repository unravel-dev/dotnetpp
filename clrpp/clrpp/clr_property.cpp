#include "clr_property.h"
#include "clr_exception.h"
#include "clr_method.h"
#include "clr_object.h"

#include <unordered_map>

namespace clr
{

namespace
{

enum property_flag_bits : int32_t
{
	flag_static = 1 << 0,
	flag_readonly = 1 << 1,
	flag_special_name = 1 << 3,
	flag_has_default = 1 << 4,
};

auto visibility_from_flags(int32_t flags) -> visibility
{
	switch((flags >> 8) & 0x7)
	{
		case 0:
			return visibility::vis_private;
		case 1:
			return visibility::vis_protected_internal;
		case 2:
			return visibility::vis_internal;
		case 3:
			return visibility::vis_protected;
		case 4:
			return visibility::vis_public;
		default:
			return visibility::vis_private;
	}
}

auto get_property_cache() -> std::unordered_map<clr_handle, std::shared_ptr<clr_property::meta_info>>&
{
	static std::unordered_map<clr_handle, std::shared_ptr<clr_property::meta_info>> cache;
	return cache;
}

} // namespace

clr_property::clr_property(clr_handle property_handle)
	: property_(property_handle)
{
	if(property_)
	{
		type_ = clr_type(bridge().property_get_type(property_));
		generate_meta(clr_type(bridge().property_get_declaring_type(property_)));
	}
}

clr_property::clr_property(const clr_type& type, const std::string& name)
{
	property_ = bridge().type_get_property(type.get_internal_ptr(), name.c_str());

	if(!property_)
	{
		throw clr_exception("NATIVE::Could not get property : " + name + " for class " + type.get_name());
	}

	type_ = clr_type(bridge().property_get_type(property_));
	generate_meta(type);
}

void clr_property::generate_meta(const clr_type& declaring_type)
{
	auto& cache = get_property_cache();
	auto it = cache.find(property_);
	if(it != cache.end())
	{
		meta_ = it->second;
		return;
	}

	auto meta = std::make_shared<meta_info>();
	meta->name = take_string(bridge().property_get_name(property_));
	meta->fullname = declaring_type.get_fullname() + "." + meta->name;
	meta->flags = bridge().property_get_flags(property_);

	std::string storage = ((meta->flags & flag_static) != 0 ? " static " : " ");
	meta->full_declname = to_string(visibility_from_flags(meta->flags)) + storage + meta->fullname;

	cache[property_] = meta;
	meta_ = meta;
}

auto clr_property::get_name() const -> std::string
{
	return meta_ ? meta_->name : std::string{};
}

auto clr_property::get_fullname() const -> std::string
{
	return meta_ ? meta_->fullname : std::string{};
}

auto clr_property::get_full_declname() const -> std::string
{
	return meta_ ? meta_->full_declname : std::string{};
}

auto clr_property::get_type() const -> const clr_type&
{
	return type_;
}

auto clr_property::get_get_method() const -> clr_method
{
	auto handle = bridge().property_get_get_method(property_);
	if(!handle)
	{
		throw clr_exception("NATIVE::Property " + get_name() + " has no getter");
	}
	return clr_method(handle);
}

auto clr_property::get_set_method() const -> clr_method
{
	auto handle = bridge().property_get_set_method(property_);
	if(!handle)
	{
		throw clr_exception("NATIVE::Property " + get_name() + " has no setter");
	}
	return clr_method(handle);
}

auto clr_property::get_visibility() const -> visibility
{
	return meta_ ? visibility_from_flags(meta_->flags) : visibility::vis_private;
}

auto clr_property::is_static() const -> bool
{
	return meta_ && (meta_->flags & flag_static) != 0;
}

auto clr_property::is_readonly() const -> bool
{
	return meta_ && (meta_->flags & flag_readonly) != 0;
}

auto clr_property::get_attributes() const -> std::vector<clr_object>
{
	std::vector<clr_object> result;
	if(!property_)
	{
		return result;
	}

	auto count = bridge().property_get_attributes(property_, nullptr, 0);
	std::vector<clr_handle> handles(static_cast<size_t>(count > 0 ? count : 0));
	if(count > 0)
	{
		bridge().property_get_attributes(property_, handles.data(), count);
	}

	result.reserve(handles.size());
	for(auto handle : handles)
	{
		result.emplace_back(clr_object(managed_ptr::adopt(handle)));
	}
	return result;
}

auto clr_property::has_attribute_fullname(const std::string& attribute_full_name) const -> bool
{
	return get_attribute_fullname(attribute_full_name).valid();
}

auto clr_property::has_attribute(const std::string& attribute_name) const -> bool
{
	return get_attribute(attribute_name).valid();
}

auto clr_property::get_attribute_fullname(const std::string& attribute_full_name) const -> clr_object
{
	for(auto& attr : get_attributes())
	{
		if(attr.get_type().get_fullname() == attribute_full_name)
		{
			return attr;
		}
	}
	return {};
}

auto clr_property::get_attribute(const std::string& attribute_name) const -> clr_object
{
	for(auto& attr : get_attributes())
	{
		if(attr.get_type().get_name() == attribute_name)
		{
			return attr;
		}
	}
	return {};
}

auto clr_property::is_special_name() const -> bool
{
	return meta_ && (meta_->flags & flag_special_name) != 0;
}

auto clr_property::has_default() const -> bool
{
	return meta_ && (meta_->flags & flag_has_default) != 0;
}

auto clr_property::get_internal_ptr() const -> clr_handle
{
	return property_;
}

void reset_property_cache()
{
	get_property_cache().clear();
}

} // namespace clr
