#include "mono_domain.h"
#include "mono_assembly.h"

#include "mono_string.h"
#include "mono_method_invoker.h"
#include "mono_type.h"
#include "mono_method.h"
#include "mono_property.h"
#include "mono_field.h"

BEGIN_MONO_INCLUDE
#include <mono/metadata/appdomain.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/mono-gc.h>
#include <mono/metadata/threads.h>
END_MONO_INCLUDE

namespace mono
{

namespace
{
auto mono_managed_gc_collect(std::string& err) -> bool
{
	try
	{
		mono_type gc_class(mono_get_corlib(), "System", "GC");

		auto collect_method = mono::make_method_invoker<void()>(gc_class, "Collect");
		auto wait_for_pending_finalizers_method = mono::make_method_invoker<void()>(gc_class, "WaitForPendingFinalizers");

		collect_method();
		wait_for_pending_finalizers_method();
		return true;

	}
	catch(const std::exception& e)
	{
		err = e.what();
		return false;
	}
}

} // namespace
static const mono_domain* current_domain = nullptr;

auto mono_domain::equals(const mono_domain& other) const -> bool
{
	return get_internal_ptr() == other.get_internal_ptr();
}

auto mono_domain::get_internal_ptr() const -> MonoDomain*
{
	return domain_;
}

void mono_domain::set_current_domain(const mono_domain& domain)
{
	set_current_domain(&domain);
}

void mono_domain::set_current_domain(const mono_domain* domain)
{
	current_domain = domain;

	if(current_domain)
	{
		mono_domain_set(current_domain->domain_, 0);
	}
	else
	{
		auto root_domain = mono_get_root_domain();
		if(root_domain)
		{
			mono_domain_set(root_domain, 0);
		}
	}
}

void mono_domain::set_assemblies_path(const std::string& path)
{
	mono_set_assemblies_path(path.c_str());
}

auto mono_domain::get_current_domain() -> const mono_domain&
{
	return *current_domain;
}

auto mono_domain::new_string(const std::string& str) const -> mono_string
{
	return mono_string(*this, str);
}

mono_domain::mono_domain(const std::string& name)
{
	domain_ = mono_domain_create_appdomain(const_cast<char*>(name.c_str()), nullptr);

	auto res = mono_domain_set(domain_, 0);
	if(res)
	{
		mono_thread_attach(domain_);
	}
}

mono_domain::~mono_domain()
{
	if(domain_)
	{
		std::string err;
		if(mono_managed_gc_collect(err))
		{
			auto root_domain = mono_get_root_domain();

			bool res = true;
			if(mono_domain_get() == domain_ && mono_domain_get() != root_domain)
			{
				res = mono_domain_set(root_domain, 0);
			}

			if(res)
			{
				mono_domain_unload(domain_);
			}
		}
	}
	mono_gc_collect(mono_gc_max_generation());
	reset_type_cache();
	reset_method_cache();
	reset_property_cache();
	reset_field_cache();
}

auto mono_domain::get_assembly(const std::string& path, bool shared) const -> mono_assembly
{
	auto it = assemblies_.find(path);
	if(it != assemblies_.end())
	{
		const auto& assembly = it->second;

		return assembly;
	}
	auto res = assemblies_.emplace(path, mono_assembly{*this, path, shared});

	const auto& assembly = res.first->second;

	return assembly;
}

auto mono_domain::get_type(const std::string& name) const -> mono_type
{
	for(const auto& assembly : assemblies_)
	{
		auto type = assembly.second.get_type(name);
		if(type.valid())
		{
			return type;
		}
	}

	auto type = mono_assembly::get_corlib().get_type(name);
	if(type.valid())
	{
		return type;
	}
	return {};
}

auto mono_domain::get_type(const std::string& name_space, const std::string& name) const -> mono_type
{
	for(const auto& assembly : assemblies_)
	{
		auto type = assembly.second.get_type(name_space, name);
		if(type.valid())
		{
			return type;
		}
	}
	
	auto type = mono_assembly::get_corlib().get_type(name_space, name);
	if(type.valid())
	{
		return type;
	}
	return {};
}

auto mono_domain::get_name() const -> std::string
{
	return mono_domain_get_friendly_name(domain_);
}

auto mono_domain::get_version() const -> uint64_t
{
	return static_cast<uint64_t>(reinterpret_cast<intptr_t>(get_internal_ptr()));
}

} // namespace mono
