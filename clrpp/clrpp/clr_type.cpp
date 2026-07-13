#include "clr_type.h"
#include "clr_bridge_utils.h"
#include "clr_domain.h"
#include "clr_exception.h"
#include "clr_field.h"
#include "clr_member_utils.h"
#include "clr_method.h"
#include "clr_object.h"
#include "clr_property.h"

#include <functional>
#include <unordered_map>

namespace clr
{

namespace
{

enum type_flag_bits : int32_t
{
	flag_valuetype = 1 << 0,
	flag_enum = 1 << 1,
	flag_class = 1 << 2,
	flag_abstract = 1 << 3,
	flag_sealed = 1 << 4,
	flag_interface = 1 << 5,
	flag_serializable = 1 << 6,
	flag_string = 1 << 7,
	flag_list = 1 << 8,
	flag_array = 1 << 9,
};

auto get_type_cache() -> std::unordered_map<clr_handle, std::shared_ptr<clr_type::meta_info>>&
{
	static std::unordered_map<clr_handle, std::shared_ptr<clr_type::meta_info>> cache;
	return cache;
}

} // namespace

clr_type::clr_type() = default;

clr_type::clr_type(clr_handle type_handle)
	: type_(type_handle)
{
	if(type_)
	{
		generate_meta();
	}
}

void clr_type::generate_meta()
{
	meta_ = get_or_create_meta<meta_info>(get_type_cache(), type_,
										  [this](meta_info& meta)
										  {
											  meta.name_space = take_string(bridge().type_get_namespace(type_));
											  meta.name = take_string(bridge().type_get_name(type_));
											  meta.fullname = take_string(bridge().type_get_fullname(type_));
											  meta.flags = bridge().type_get_flags(type_);
										  });
}

auto clr_type::valid() const -> bool
{
	return type_ != nullptr;
}

auto clr_type::new_instance() const -> clr_object
{
	clr_exception_info_raw ex{};
	auto handle = bridge().object_create(type_, &ex);
	throw_if_exception(ex);
	return clr_object(managed_ptr::adopt(handle), *this);
}

auto clr_type::new_instance(const clr_domain& domain) const -> clr_object
{
	(void)domain; // instances are not domain-affine on coreclr
	return new_instance();
}

auto clr_type::get_method(const std::string& name_with_args) const -> clr_method
{
	return clr_method(*this, name_with_args);
}

auto clr_type::get_method(const std::string& name, int argc) const -> clr_method
{
	return clr_method(*this, name, argc);
}

auto clr_type::get_field(const std::string& name) const -> clr_field
{
	return clr_field(*this, name);
}

auto clr_type::get_property(const std::string& name) const -> clr_property
{
	return clr_property(*this, name);
}

auto clr_type::get_fields(bool include_base) const -> std::vector<clr_field>
{
	if(!valid())
	{
		return {};
	}

	const int32_t include = include_base ? 1 : 0;
	return fetch_and_map<clr_field>(
		[this, include](clr_handle* buffer, int32_t count)
		{ return bridge().type_get_fields(type_, include, buffer, count); },
		[](clr_handle handle) { return clr_field(handle); });
}

auto clr_type::get_properties(bool include_base) const -> std::vector<clr_property>
{
	if(!valid())
	{
		return {};
	}

	const int32_t include = include_base ? 1 : 0;
	return fetch_and_map<clr_property>(
		[this, include](clr_handle* buffer, int32_t count)
		{ return bridge().type_get_properties(type_, include, buffer, count); },
		[](clr_handle handle) { return clr_property(handle); });
}

auto clr_type::get_methods(bool include_base) const -> std::vector<clr_method>
{
	if(!valid())
	{
		return {};
	}

	const int32_t include = include_base ? 1 : 0;
	return fetch_and_map<clr_method>(
		[this, include](clr_handle* buffer, int32_t count)
		{ return bridge().type_get_methods(type_, include, buffer, count); },
		[](clr_handle handle) { return clr_method(handle); });
}

auto clr_type::get_attributes(bool include_base) const -> std::vector<clr_object>
{
	if(!valid())
	{
		return {};
	}

	const int32_t include = include_base ? 1 : 0;
	return fetch_managed_objects(
		[this, include](clr_handle* buffer, int32_t count)
		{ return bridge().type_get_attributes(type_, include, buffer, count); });
}

auto clr_type::has_base_type() const -> bool
{
	return valid() && bridge().type_get_base_type(type_) != nullptr;
}

auto clr_type::get_base_type() const -> clr_type
{
	if(!valid())
	{
		return {};
	}
	auto handle = bridge().type_get_base_type(type_);
	return handle ? clr_type(handle) : clr_type();
}

auto clr_type::get_nested_types() const -> std::vector<clr_type>
{
	if(!valid())
	{
		return {};
	}

	return fetch_and_map<clr_type>(
		[this](clr_handle* buffer, int32_t count)
		{ return bridge().type_get_nested_types(type_, buffer, count); },
		[](clr_handle handle) { return clr_type(handle); });
}

auto clr_type::get_nesting_type() const -> clr_type
{
	if(!valid())
	{
		return {};
	}
	auto handle = bridge().type_get_nesting_type(type_);
	return handle ? clr_type(handle) : clr_type();
}

auto clr_type::is_derived_from(const clr_type& type) const -> bool
{
	if(!valid() || !type.valid())
	{
		return false;
	}
	return bridge().type_is_derived_from(type_, type.type_) != 0;
}

auto clr_type::get_namespace() const -> std::string
{
	return meta_ ? meta_->name_space : std::string{};
}

auto clr_type::get_name() const -> std::string
{
	return meta_ ? meta_->name : std::string{};
}

auto clr_type::get_fullname() const -> std::string
{
	return meta_ ? meta_->fullname : std::string{};
}

auto clr_type::get_hash() const -> size_t
{
	return get_hash(get_fullname());
}

auto clr_type::get_hash(const std::string& name) -> size_t
{
	return std::hash<std::string>{}(name);
}

auto clr_type::get_hash(const char* name) -> size_t
{
	return get_hash(std::string(name ? name : ""));
}

auto clr_type::is_valuetype() const -> bool
{
	return meta_ && (meta_->flags & flag_valuetype) != 0;
}

auto clr_type::is_struct() const -> bool
{
	return is_valuetype() && !is_enum();
}

auto clr_type::is_class() const -> bool
{
	return meta_ && (meta_->flags & flag_class) != 0;
}

auto clr_type::is_enum() const -> bool
{
	return meta_ && (meta_->flags & flag_enum) != 0;
}

auto clr_type::get_enum_base_type() const -> clr_type
{
	if(!valid())
	{
		return {};
	}
	auto handle = bridge().type_get_enum_base_type(type_);
	return handle ? clr_type(handle) : clr_type();
}

auto clr_type::get_rank() const -> int
{
	return valid() ? bridge().type_get_rank(type_) : 0;
}

auto clr_type::is_array() const -> bool
{
	return meta_ && (meta_->flags & flag_array) != 0;
}

auto clr_type::get_element_type() const -> clr_type
{
	if(!valid())
	{
		return {};
	}
	auto handle = bridge().type_get_element_type(type_);
	return handle ? clr_type(handle) : clr_type();
}

auto clr_type::get_sizeof() const -> std::uint32_t
{
	return valid() ? static_cast<std::uint32_t>(bridge().type_get_sizeof(type_)) : 0;
}

auto clr_type::get_alignof() const -> std::uint32_t
{
	return valid() ? static_cast<std::uint32_t>(bridge().type_get_alignof(type_)) : 0;
}

auto clr_type::is_abstract() const -> bool
{
	return meta_ && (meta_->flags & flag_abstract) != 0;
}

auto clr_type::is_sealed() const -> bool
{
	return meta_ && (meta_->flags & flag_sealed) != 0;
}

auto clr_type::is_interface() const -> bool
{
	return meta_ && (meta_->flags & flag_interface) != 0;
}

auto clr_type::is_serializable() const -> bool
{
	return meta_ && (meta_->flags & flag_serializable) != 0;
}

auto clr_type::is_string() const -> bool
{
	return meta_ && (meta_->flags & flag_string) != 0;
}

auto clr_type::is_list() const -> bool
{
	return meta_ && (meta_->flags & flag_list) != 0;
}

auto clr_type::get_list_type() const -> clr_type
{
	if(!valid())
	{
		return {};
	}
	auto handle = bridge().type_get_list_type(type_);
	return handle ? clr_type(handle) : clr_type();
}

auto clr_type::get_internal_ptr() const -> clr_handle
{
	return type_;
}

void reset_type_cache()
{
	get_type_cache().clear();
}

} // namespace clr
