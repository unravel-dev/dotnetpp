#include "clr_property.h"
#include "clr_bridge_utils.h"
#include "clr_exception.h"
#include "clr_member_flags.h"
#include "clr_member_utils.h"
#include "clr_method.h"

#include <unordered_map>

namespace clr
{

namespace
{
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
	meta_ = get_or_create_meta<meta_info>(get_property_cache(), property_,
										  [&](meta_info& meta)
										  {
											  meta.name = take_string(bridge().property_get_name(property_));
											  meta.fullname = declaring_type.get_fullname() + "." + meta.name;
											  meta.flags = bridge().property_get_flags(property_);
											  meta.full_declname = make_member_full_declname(meta.flags, meta.fullname);
										  });
}

auto clr_property::get_name() const -> std::string
{
	return meta_name(meta_);
}

auto clr_property::get_fullname() const -> std::string
{
	return meta_fullname(meta_);
}

auto clr_property::get_full_declname() const -> std::string
{
	return meta_full_declname(meta_);
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
	return meta_ && has_member_flag_static(meta_->flags);
}

auto clr_property::is_readonly() const -> bool
{
	return meta_ && (meta_->flags & property_flag_readonly) != 0;
}

auto clr_property::get_attributes() const -> std::vector<clr_object>
{
	if(!property_ || !meta_)
	{
		return {};
	}
	if(!meta_->attributes_cached)
	{
		meta_->attributes = fetch_managed_objects(
			[this](clr_handle* buffer, int32_t count)
			{ return bridge().property_get_attributes(property_, buffer, count); });
		meta_->attributes_cached = true;
	}
	return meta_->attributes;
}

auto clr_property::has_attribute_fullname(const std::string& attribute_full_name) const -> bool
{
	return has_attribute_by_fullname(get_attributes(), attribute_full_name);
}

auto clr_property::has_attribute(const std::string& attribute_name) const -> bool
{
	return has_attribute_by_name(get_attributes(), attribute_name);
}

auto clr_property::get_attribute_fullname(const std::string& attribute_full_name) const -> clr_object
{
	return find_attribute_by_fullname(get_attributes(), attribute_full_name);
}

auto clr_property::get_attribute(const std::string& attribute_name) const -> clr_object
{
	return find_attribute_by_name(get_attributes(), attribute_name);
}

auto clr_property::is_special_name() const -> bool
{
	return meta_ && (meta_->flags & property_flag_special_name) != 0;
}

auto clr_property::has_default() const -> bool
{
	return meta_ && (meta_->flags & property_flag_has_default) != 0;
}

auto clr_property::is_valid() const -> bool
{
	return property_ != nullptr;
}

auto clr_property::equals(const clr_property& other) const -> bool
{
	return bridge().handle_equals(get_internal_ptr(), other.get_internal_ptr()) != 0;
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
