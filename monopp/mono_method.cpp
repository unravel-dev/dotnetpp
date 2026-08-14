#include "mono_method.h"
#include "mono_exception.h"
#include "mono_type.h"

#include <mutex>
#include <unordered_map>

BEGIN_MONO_INCLUDE
#include <mono/metadata/appdomain.h>
#include <mono/metadata/attrdefs.h>
#include <mono/metadata/debug-helpers.h>
END_MONO_INCLUDE

namespace mono
{
struct mono_method::meta_info
{
	std::string name;
	std::string fullname;
	std::string full_declname;
};

namespace
{

auto get_method_cache() -> std::unordered_map<MonoMethod*, std::shared_ptr<mono_method::meta_info>>&
{
	static std::unordered_map<MonoMethod*, std::shared_ptr<mono_method::meta_info>> method_cache;
	return method_cache;
}

// Guards this meta cache: wrappers are constructed off the main thread too
// (e.g. compile jobs), and unordered_map mutation is a crash-grade race.
auto get_meta_cache_mutex() -> std::mutex&
{
	static std::mutex m;
	return m;
}

auto get_meta_info(MonoMethod* method) -> std::shared_ptr<mono_method::meta_info>
{
	std::lock_guard<std::mutex> lock(get_meta_cache_mutex());
	auto& cache = get_method_cache();
	auto it = cache.find(method);
	if(it != cache.end())
	{
		return it->second;
	}
	return nullptr;
}

void set_meta_info(MonoMethod* method, std::shared_ptr<mono_method::meta_info> meta)
{
	std::lock_guard<std::mutex> lock(get_meta_cache_mutex());
	auto& cache = get_method_cache();
	cache[method] = meta;
}

} // namespace

mono_method::mono_method(MonoMethod* method)
{
	method_ = method;
	if(!method_)
	{
		return;
	}
	signature_ = mono_method_signature(method_);

	generate_meta();
}

mono_method::mono_method(const mono_type& type, const std::string& name_with_args)
{
	auto desc = mono_method_desc_new((":" + name_with_args).c_str(), 0);

	auto check_type = type;
	while(!method_ && check_type.valid())
	{
		method_ = mono_method_desc_search_in_class(desc, check_type.get_internal_ptr());
		check_type = check_type.get_base_type();
	}

	mono_method_desc_free(desc);

	if(!method_)
	{
		throw mono_exception("NATIVE::Could not get method : " + name_with_args + " for class " + type.get_name());
	}
	signature_ = mono_method_signature(method_);

	generate_meta();
}

mono_method::mono_method(const mono_type& type, const std::string& name, int argc)
{
	auto check_type = type;
	while(!method_ && check_type.valid())
	{
		method_ = mono_class_get_method_from_name(check_type.get_internal_ptr(), name.c_str(), argc);
		check_type = check_type.get_base_type();
	}

	if(!method_)
	{
		throw mono_exception("NATIVE::Could not get method : " + name + " for class " + type.get_name());
	}
	signature_ = mono_method_signature(method_);

	generate_meta();
}

auto mono_method::equals(const mono_method& other) const -> bool
{
	return get_internal_ptr() == other.get_internal_ptr();
}

auto mono_method::get_internal_ptr() const -> MonoMethod*
{
	return method_;
}


void mono_method::generate_meta()
{
	auto meta = get_meta_info(method_);
	if(!meta)
	{
		meta = std::make_shared<meta_info>();
		meta->name = get_name();
		meta->fullname = get_fullname();
		meta->full_declname = get_full_declname();
		set_meta_info(method_, meta);
	}

	meta_ = meta;
}

auto mono_method::get_return_type() const -> mono_type
{
	auto type = mono_signature_get_return_type(signature_);
	return mono_type(type);
}

void mono_method::cache_param_types() const
{
	if (!param_types_cached_)
	{
		void* iter = nullptr;
		auto type = mono_signature_get_params(signature_, &iter);
		while(type)
		{
			cached_param_types_.emplace_back(type);
			type = mono_signature_get_params(signature_, &iter);
		}
		param_types_cached_ = true;
	}
}

auto mono_method::get_param_types() const -> const std::vector<mono_type>&
{
	cache_param_types();
	return cached_param_types_;
}

auto mono_method::get_name() const -> std::string
{
	if(meta_)
	{
		return meta_->name;
	}
	return mono_method_get_name(method_);
}

auto mono_method::get_fullname() const -> std::string
{
	if(meta_)
	{
		return meta_->fullname;
	}
	char* mono_name = mono_method_full_name(method_, true);
	std::string name(mono_name);
	mono_free(mono_name);
	return name;
}
auto mono_method::get_full_declname() const -> std::string
{
	if(meta_)
	{
		return meta_->full_declname;
	}
	std::string storage = (is_static() ? " static " : " ");
	return to_string(get_visibility()) + storage + get_fullname();
}
auto mono_method::get_visibility() const -> visibility
{

	uint32_t flags = mono_method_get_flags(method_, nullptr) & MONO_METHOD_ATTR_ACCESS_MASK;

	if(flags == MONO_METHOD_ATTR_PRIVATE)
		return visibility::vis_private;
	else if(flags == MONO_METHOD_ATTR_FAM_AND_ASSEM)
		return visibility::vis_protected_internal;
	else if(flags == MONO_METHOD_ATTR_ASSEM)
		return visibility::vis_internal;
	else if(flags == MONO_METHOD_ATTR_FAMILY)
		return visibility::vis_protected;
	else if(flags == MONO_METHOD_ATTR_PUBLIC)
		return visibility::vis_public;

	assert(false);

	return visibility::vis_private;
}

auto mono_method::is_static() const -> bool
{
	uint32_t flags = mono_method_get_flags(method_, nullptr);
	return (flags & MONO_METHOD_ATTR_STATIC) != 0;
}
auto mono_method::is_virtual() const -> bool
{
	uint32_t flags = mono_method_get_flags(method_, nullptr);
	return (flags & MONO_METHOD_ATTR_VIRTUAL) != 0;
}

auto mono_method::is_pinvoke_impl() const -> bool
{
	uint32_t flags = mono_method_get_flags(method_, nullptr);
	return (flags & MONO_METHOD_ATTR_PINVOKE_IMPL) != 0;
}

auto mono_method::is_special_name() const -> bool
{
	uint32_t flags = mono_method_get_flags(method_, nullptr);
	return (flags & MONO_METHOD_ATTR_SPECIAL_NAME) != 0;
}

auto mono_method::is_internal_call() const -> bool
{
	uint32_t impl_flags = 0;
	mono_method_get_flags(method_, &impl_flags);
	return (impl_flags & MONO_METHOD_IMPL_ATTR_INTERNAL_CALL) != 0;
}

auto mono_method::is_synchronized() const -> bool
{
	uint32_t impl_flags = 0;
	mono_method_get_flags(method_, &impl_flags);
	return (impl_flags & MONO_METHOD_IMPL_ATTR_SYNCHRONIZED) != 0;
}

auto mono_method::get_attributes() const -> std::vector<mono_type>
{
	std::vector<mono_type> result;

	// Get custom attributes from the method
	MonoCustomAttrInfo* attr_info = mono_custom_attrs_from_method(method_);

	if(attr_info)
	{
		result.reserve(attr_info->num_attrs);

		// Iterate over the custom attributes
		for(int i = 0; i < attr_info->num_attrs; ++i)
		{
			MonoCustomAttrEntry* entry = &attr_info->attrs[i];

			// Get the MonoClass* of the attribute
			MonoClass* attr_class = mono_method_get_class(entry->ctor);

			result.emplace_back(attr_class);
		}

		// Free the attribute info when done
		mono_custom_attrs_free(attr_info);
	}

	// Get method flags
	uint32_t impl_flags{};
	uint32_t flags = mono_method_get_flags(method_, &impl_flags);

	MonoImage* corlib = mono_get_corlib(); // Get corlib once for efficiency

	// Check for SpecialName
	if((flags & MONO_METHOD_ATTR_SPECIAL_NAME) != 0)
	{
		MonoClass* special_name_attr_class =
			mono_class_from_name(corlib, "System.Runtime.CompilerServices", "SpecialNameAttribute");
		if(special_name_attr_class)
		{
			result.emplace_back(special_name_attr_class);
		}
	}

	// Check for PInvokeImpl
	if((flags & MONO_METHOD_ATTR_PINVOKE_IMPL) != 0)
	{
		MonoClass* dll_import_attr_class =
			mono_class_from_name(corlib, "System.Runtime.InteropServices", "DllImportAttribute");
		if(dll_import_attr_class)
		{
			result.emplace_back(dll_import_attr_class);
		}
	}

	// Check for MethodImpl attributes

	if((impl_flags & MONO_METHOD_IMPL_ATTR_INTERNAL_CALL) != 0)
	{
		// InternalCall methods may correspond to [MethodImpl(MethodImplOptions.InternalCall)]
		// There is no direct attribute class, but you can note this flag
	}

	if((impl_flags & MONO_METHOD_IMPL_ATTR_SYNCHRONIZED) != 0)
	{
		// Synchronized methods correspond to [MethodImpl(MethodImplOptions.Synchronized)]
		MonoClass* method_impl_attr_class =
			mono_class_from_name(corlib, "System.Runtime.CompilerServices", "MethodImplAttribute");
		if(method_impl_attr_class)
		{
			result.emplace_back(method_impl_attr_class);
		}
	}

	// Add more checks for other MethodImplOptions as needed

	return result;
}

auto mono_method::valid() const -> bool
{
	return method_ != nullptr;
}

mono_method::operator bool() const
{
	return valid();
}
void reset_method_cache()
{
	std::lock_guard<std::mutex> lock(get_meta_cache_mutex());
	auto& cache = get_method_cache();
	cache.clear();
}
} // namespace mono
