#include "clr_field.h"
#include "clr_bridge_utils.h"
#include "clr_exception.h"
#include "clr_member_flags.h"
#include "clr_member_utils.h"

#include <unordered_map>

namespace clr
{

namespace
{
auto get_field_cache() -> std::unordered_map<clr_handle, std::shared_ptr<clr_field::meta_info>>&
{
	static std::unordered_map<clr_handle, std::shared_ptr<clr_field::meta_info>> cache;
	return cache;
}
} // namespace

clr_field::clr_field(clr_handle field_handle)
	: field_(field_handle)
{
	if(field_)
	{
		type_ = clr_type(bridge().field_get_type(field_));
		generate_meta(clr_type(bridge().field_get_declaring_type(field_)));
	}
}

clr_field::clr_field(const clr_type& type, const std::string& name)
{
	field_ = bridge().type_get_field(type.get_internal_ptr(), name.c_str());

	if(!field_)
	{
		throw clr_exception("NATIVE::Could not get field : " + name + " for class " + type.get_name());
	}

	type_ = clr_type(bridge().field_get_type(field_));
	generate_meta(type);
}

void clr_field::generate_meta(const clr_type& declaring_type)
{
	meta_ = get_or_create_meta<meta_info>(get_field_cache(), field_,
										  [&](meta_info& meta)
										  {
											  meta.name = take_string(bridge().field_get_name(field_));
											  meta.fullname = declaring_type.get_fullname() + "." + meta.name;
											  meta.flags = bridge().field_get_flags(field_);
											  meta.full_declname = make_member_full_declname(meta.flags, meta.fullname);
										  });
}

auto clr_field::get_name() const -> std::string
{
	return meta_name(meta_);
}

auto clr_field::get_fullname() const -> std::string
{
	return meta_fullname(meta_);
}

auto clr_field::get_full_declname() const -> std::string
{
	return meta_full_declname(meta_);
}

auto clr_field::get_type() const -> const clr_type&
{
	return type_;
}

auto clr_field::get_visibility() const -> visibility
{
	return meta_ ? visibility_from_flags(meta_->flags) : visibility::vis_private;
}

auto clr_field::is_static() const -> bool
{
	return meta_ && has_member_flag_static(meta_->flags);
}

auto clr_field::get_attributes() const -> std::vector<clr_object>
{
	if(!field_ || !meta_)
	{
		return {};
	}
	if(!meta_->attributes_cached)
	{
		meta_->attributes = fetch_managed_objects(
			[this](clr_handle* buffer, int32_t count)
			{ return bridge().field_get_attributes(field_, buffer, count); });
		meta_->attributes_cached = true;
	}
	return meta_->attributes;
}

auto clr_field::has_attribute_fullname(const std::string& attribute_full_name) const -> bool
{
	return has_attribute_by_fullname(get_attributes(), attribute_full_name);
}

auto clr_field::has_attribute(const std::string& attribute_name) const -> bool
{
	return has_attribute_by_name(get_attributes(), attribute_name);
}

auto clr_field::get_attribute_fullname(const std::string& attribute_full_name) const -> clr_object
{
	return find_attribute_by_fullname(get_attributes(), attribute_full_name);
}

auto clr_field::get_attribute(const std::string& attribute_name) const -> clr_object
{
	return find_attribute_by_name(get_attributes(), attribute_name);
}

auto clr_field::is_readonly() const -> bool
{
	return meta_ && (meta_->flags & field_flag_readonly) != 0;
}

auto clr_field::is_const() const -> bool
{
	return meta_ && (meta_->flags & field_flag_const) != 0;
}

auto clr_field::is_backing_field() const -> bool
{
	return meta_ && (meta_->flags & field_flag_backing) != 0;
}

auto clr_field::is_valuetype() const -> bool
{
	return type_.is_valuetype();
}

auto clr_field::get_internal_ptr() const -> clr_handle
{
	return field_;
}

void reset_field_cache()
{
	get_field_cache().clear();
}

} // namespace clr
