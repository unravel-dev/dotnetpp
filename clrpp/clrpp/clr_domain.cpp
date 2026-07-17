#include "clr_domain.h"
#include "clr_exception.h"
#include "clr_field.h"
#include "clr_method.h"
#include "clr_property.h"
#include "clr_string.h"
#include "clr_type.h"

namespace clr
{

namespace
{
const clr_domain* current_domain = nullptr;

auto get_default_search_paths() -> std::vector<std::string>&
{
	static std::vector<std::string> paths;
	return paths;
}
} // namespace

clr_domain::clr_domain(const std::string& name)
	: name_(name)
{
	domain_ = bridge().domain_create(name.c_str());
	if(!domain_)
	{
		throw clr_exception("NATIVE::Could not create domain : " + name);
	}

	for(const auto& path : get_default_search_paths())
	{
		bridge().domain_add_search_path(domain_, path.c_str());
	}
}

clr_domain::~clr_domain()
{
	if(current_domain == this)
	{
		current_domain = nullptr;
	}

	assemblies_.clear();

	if(domain_ && bridge_alive())
	{
		bridge().domain_unload(domain_);
	}
	domain_ = nullptr;

	if(bridge_alive())
	{
		bridge().gc_collect();
	}

	reset_type_cache();
	reset_method_cache();
	reset_property_cache();
	reset_field_cache();
}

void clr_domain::set_current_domain(const clr_domain& domain)
{
	set_current_domain(&domain);
}

void clr_domain::set_current_domain(const clr_domain* domain)
{
	current_domain = domain;
}

auto clr_domain::get_current_domain() -> const clr_domain&
{
	return *current_domain;
}

void clr_domain::set_assemblies_path(const std::string& path)
{
	auto& paths = get_default_search_paths();

	// Accept a platform path list (';' on windows, ':' elsewhere).
#ifdef _WIN32
	const char separator = ';';
#else
	const char separator = ':';
#endif

	std::string current;
	for(char c : path)
	{
		if(c == separator)
		{
			if(!current.empty())
			{
				paths.push_back(current);
			}
			current.clear();
		}
		else
		{
			current += c;
		}
	}
	if(!current.empty())
	{
		paths.push_back(current);
	}
}

auto clr_domain::get_assembly(const std::string& path, bool shared) const -> clr_assembly
{
	auto it = assemblies_.find(path);
	if(it != assemblies_.end())
	{
		return it->second;
	}
	auto res = assemblies_.emplace(path, clr_assembly{*this, path, shared});
	return res.first->second;
}

auto clr_domain::get_type(const std::string& name) const -> clr_type
{
	for(const auto& assembly : assemblies_)
	{
		auto type = assembly.second.get_type(name);
		if(type.valid())
		{
			return type;
		}
	}

	auto type = clr_assembly::get_corlib().get_type(name);
	if(type.valid())
	{
		return type;
	}
	return {};
}

auto clr_domain::get_type(const std::string& name_space, const std::string& name) const -> clr_type
{
	for(const auto& assembly : assemblies_)
	{
		auto type = assembly.second.get_type(name_space, name);
		if(type.valid())
		{
			return type;
		}
	}

	auto type = clr_assembly::get_corlib().get_type(name_space, name);
	if(type.valid())
	{
		return type;
	}
	return {};
}

auto clr_domain::new_string(const std::string& str) const -> clr_string
{
	return clr_string(*this, str);
}

auto clr_domain::get_name() const -> std::string
{
	return name_;
}

auto clr_domain::equals(const clr_domain& other) const -> bool
{
	return bridge().handle_equals(get_internal_ptr(), other.get_internal_ptr()) != 0;
}

auto clr_domain::get_internal_ptr() const -> clr_handle
{
	return domain_;
}

auto clr_domain::get_version() const -> uint64_t
{
	return static_cast<uint64_t>(reinterpret_cast<intptr_t>(get_internal_ptr()));
}

} // namespace clr
