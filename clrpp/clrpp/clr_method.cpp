#include "clr_method.h"
#include "clr_bridge_utils.h"
#include "clr_exception.h"
#include "clr_member_flags.h"
#include "clr_member_meta.h"
#include "clr_member_utils.h"

#include <unordered_map>

namespace clr
{

namespace
{
auto get_method_cache() -> std::unordered_map<clr_handle, std::shared_ptr<clr_method::meta_info>>&
{
	static std::unordered_map<clr_handle, std::shared_ptr<clr_method::meta_info>> cache;
	return cache;
}
} // namespace

clr_method::clr_method(clr_handle method_handle)
	: method_(method_handle)
{
	if(method_)
	{
		generate_meta();
	}
}

clr_method::clr_method(const clr_type& type, const std::string& name_with_args)
{
	method_ = bridge().type_get_method_by_signature(type.get_internal_ptr(), name_with_args.c_str());

	if(!method_)
	{
		throw clr_exception("NATIVE::Could not get method : " + name_with_args + " for class " +
							type.get_name());
	}

	generate_meta();
}

clr_method::clr_method(const clr_type& type, const std::string& name, int argc)
{
	method_ = bridge().type_get_method(type.get_internal_ptr(), name.c_str(), argc);

	if(!method_)
	{
		throw clr_exception("NATIVE::Could not get method : " + name + " for class " + type.get_name());
	}

	generate_meta();
}

void clr_method::generate_meta()
{
	meta_ = get_or_create_meta<meta_info>(get_method_cache(), method_,
										  [this](meta_info& meta)
										  {
											  meta.name = take_string(bridge().method_get_name(method_));
											  meta.fullname = take_string(bridge().method_get_fullname(method_));
											  meta.flags = bridge().method_get_flags(method_);
											  meta.full_declname = make_member_full_declname(meta.flags, meta.fullname);
										  });
}

auto clr_method::get_return_type() const -> clr_type
{
	auto handle = bridge().method_get_return_type(method_);
	return handle ? clr_type(handle) : clr_type();
}

void clr_method::cache_param_types() const
{
	if(param_types_cached_)
	{
		return;
	}

	cached_param_types_ = fetch_and_map<clr_type>(
		[this](clr_handle* buffer, int32_t count)
		{ return bridge().method_get_param_types(method_, buffer, count); },
		[](clr_handle handle) { return clr_type(handle); });
	param_types_cached_ = true;
}

auto clr_method::get_param_types() const -> const std::vector<clr_type>&
{
	cache_param_types();
	return cached_param_types_;
}

auto clr_method::get_name() const -> std::string
{
	return meta_name(meta_);
}

auto clr_method::get_fullname() const -> std::string
{
	return meta_fullname(meta_);
}

auto clr_method::get_full_declname() const -> std::string
{
	return meta_full_declname(meta_);
}

auto clr_method::get_visibility() const -> visibility
{
	return meta_ ? visibility_from_flags(meta_->flags) : visibility::vis_private;
}

auto clr_method::is_static() const -> bool
{
	return meta_ && has_member_flag_static(meta_->flags);
}

auto clr_method::is_virtual() const -> bool
{
	return meta_ && (meta_->flags & method_flag_virtual) != 0;
}

auto clr_method::is_pinvoke_impl() const -> bool
{
	return meta_ && (meta_->flags & method_flag_pinvoke) != 0;
}

auto clr_method::is_special_name() const -> bool
{
	return meta_ && (meta_->flags & method_flag_special_name) != 0;
}

auto clr_method::is_internal_call() const -> bool
{
	return meta_ && (meta_->flags & method_flag_internal_call) != 0;
}

auto clr_method::is_synchronized() const -> bool
{
	return meta_ && (meta_->flags & method_flag_synchronized) != 0;
}

auto clr_method::get_attributes() const -> std::vector<clr_type>
{
	if(!valid())
	{
		return {};
	}

	return fetch_and_map<clr_type>(
		[this](clr_handle* buffer, int32_t count)
		{ return bridge().method_get_attributes(method_, buffer, count); },
		[](clr_handle handle) { return clr_type(handle); });
}

auto clr_method::valid() const -> bool
{
	return method_ != nullptr;
}

clr_method::operator bool() const
{
	return valid();
}

auto clr_method::get_internal_ptr() const -> clr_handle
{
	return method_;
}

void reset_method_cache()
{
	get_method_cache().clear();
}

} // namespace clr
