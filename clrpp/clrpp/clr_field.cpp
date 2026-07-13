#include "clr_field.h"
#include "clr_exception.h"
#include "clr_object.h"

#include <unordered_map>

namespace clr
{

namespace
{

enum field_flag_bits : int32_t
{
	flag_static = 1 << 0,
	flag_readonly = 1 << 1,
	flag_const = 1 << 2,
	flag_backing = 1 << 3,
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
	auto& cache = get_field_cache();
	auto it = cache.find(field_);
	if(it != cache.end())
	{
		meta_ = it->second;
		return;
	}

	auto meta = std::make_shared<meta_info>();
	meta->name = take_string(bridge().field_get_name(field_));
	meta->fullname = declaring_type.get_fullname() + "." + meta->name;
	meta->flags = bridge().field_get_flags(field_);

	std::string storage = ((meta->flags & flag_static) != 0 ? " static " : " ");
	meta->full_declname = to_string(visibility_from_flags(meta->flags)) + storage + meta->fullname;

	cache[field_] = meta;
	meta_ = meta;
}

auto clr_field::get_name() const -> std::string
{
	return meta_ ? meta_->name : std::string{};
}

auto clr_field::get_fullname() const -> std::string
{
	return meta_ ? meta_->fullname : std::string{};
}

auto clr_field::get_full_declname() const -> std::string
{
	return meta_ ? meta_->full_declname : std::string{};
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
	return meta_ && (meta_->flags & flag_static) != 0;
}

auto clr_field::get_attributes() const -> std::vector<clr_object>
{
	std::vector<clr_object> result;
	if(!field_)
	{
		return result;
	}

	auto count = bridge().field_get_attributes(field_, nullptr, 0);
	std::vector<clr_handle> handles(static_cast<size_t>(count > 0 ? count : 0));
	if(count > 0)
	{
		bridge().field_get_attributes(field_, handles.data(), count);
	}

	result.reserve(handles.size());
	for(auto handle : handles)
	{
		result.emplace_back(clr_object(managed_ptr::adopt(handle)));
	}
	return result;
}

auto clr_field::has_attribute_fullname(const std::string& attribute_full_name) const -> bool
{
	return get_attribute_fullname(attribute_full_name).valid();
}

auto clr_field::has_attribute(const std::string& attribute_name) const -> bool
{
	return get_attribute(attribute_name).valid();
}

auto clr_field::get_attribute_fullname(const std::string& attribute_full_name) const -> clr_object
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

auto clr_field::get_attribute(const std::string& attribute_name) const -> clr_object
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

auto clr_field::is_readonly() const -> bool
{
	return meta_ && (meta_->flags & flag_readonly) != 0;
}

auto clr_field::is_const() const -> bool
{
	return meta_ && (meta_->flags & flag_const) != 0;
}

auto clr_field::is_backing_field() const -> bool
{
	return meta_ && (meta_->flags & flag_backing) != 0;
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
