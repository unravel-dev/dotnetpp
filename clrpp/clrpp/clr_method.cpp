#include "clr_method.h"
#include "clr_exception.h"

#include <unordered_map>

namespace clr
{

namespace
{

enum method_flag_bits : int32_t
{
	flag_static = 1 << 0,
	flag_virtual = 1 << 1,
	flag_pinvoke = 1 << 2,
	flag_special_name = 1 << 3,
	flag_internal_call = 1 << 4,
	flag_synchronized = 1 << 5,
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
	auto& cache = get_method_cache();
	auto it = cache.find(method_);
	if(it != cache.end())
	{
		meta_ = it->second;
		return;
	}

	auto meta = std::make_shared<meta_info>();
	meta->name = take_string(bridge().method_get_name(method_));
	meta->fullname = take_string(bridge().method_get_fullname(method_));
	meta->flags = bridge().method_get_flags(method_);

	std::string storage = ((meta->flags & flag_static) != 0 ? " static " : " ");
	meta->full_declname = to_string(visibility_from_flags(meta->flags)) + storage + meta->fullname;

	cache[method_] = meta;
	meta_ = meta;
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

	auto count = bridge().method_get_param_types(method_, nullptr, 0);
	std::vector<clr_handle> handles(static_cast<size_t>(count > 0 ? count : 0));
	if(count > 0)
	{
		bridge().method_get_param_types(method_, handles.data(), count);
	}

	cached_param_types_.reserve(handles.size());
	for(auto handle : handles)
	{
		cached_param_types_.emplace_back(clr_type(handle));
	}
	param_types_cached_ = true;
}

auto clr_method::get_param_types() const -> const std::vector<clr_type>&
{
	cache_param_types();
	return cached_param_types_;
}

auto clr_method::get_name() const -> std::string
{
	return meta_ ? meta_->name : std::string{};
}

auto clr_method::get_fullname() const -> std::string
{
	return meta_ ? meta_->fullname : std::string{};
}

auto clr_method::get_full_declname() const -> std::string
{
	return meta_ ? meta_->full_declname : std::string{};
}

auto clr_method::get_visibility() const -> visibility
{
	return meta_ ? visibility_from_flags(meta_->flags) : visibility::vis_private;
}

auto clr_method::is_static() const -> bool
{
	return meta_ && (meta_->flags & flag_static) != 0;
}

auto clr_method::is_virtual() const -> bool
{
	return meta_ && (meta_->flags & flag_virtual) != 0;
}

auto clr_method::is_pinvoke_impl() const -> bool
{
	return meta_ && (meta_->flags & flag_pinvoke) != 0;
}

auto clr_method::is_special_name() const -> bool
{
	return meta_ && (meta_->flags & flag_special_name) != 0;
}

auto clr_method::is_internal_call() const -> bool
{
	return meta_ && (meta_->flags & flag_internal_call) != 0;
}

auto clr_method::is_synchronized() const -> bool
{
	return meta_ && (meta_->flags & flag_synchronized) != 0;
}

auto clr_method::get_attributes() const -> std::vector<clr_type>
{
	std::vector<clr_type> result;
	if(!valid())
	{
		return result;
	}

	auto count = bridge().method_get_attributes(method_, nullptr, 0);
	std::vector<clr_handle> handles(static_cast<size_t>(count > 0 ? count : 0));
	if(count > 0)
	{
		bridge().method_get_attributes(method_, handles.data(), count);
	}

	result.reserve(handles.size());
	for(auto handle : handles)
	{
		result.emplace_back(clr_type(handle));
	}
	return result;
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
