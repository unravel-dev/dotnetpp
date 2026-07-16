#include "clr_assembly.h"
#include "clr_bridge_utils.h"
#include "clr_domain.h"
#include "clr_exception.h"

#include <sstream>

namespace clr
{

clr_assembly::clr_assembly(const clr_domain& domain, const std::string& path, bool shared)
{
	(void)shared; // assemblies are always shared within their load context

	clr_exception_info_raw ex{};
	assembly_ = bridge().assembly_load(domain.get_internal_ptr(), path.c_str(), &ex);
	if(ex.has_value != 0)
	{
		auto thunk = make_thunk_exception(ex);
		throw clr_exception("NATIVE::Could not open assembly with path : " + path + " (" + thunk.message() +
							")");
	}

	if(!assembly_)
	{
		throw clr_exception("NATIVE::Could not open assembly with path : " + path);
	}
}

clr_assembly::clr_assembly(clr_handle assembly_handle)
	: assembly_(assembly_handle)
{
}

auto clr_assembly::valid() const -> bool
{
	return assembly_ != nullptr;
}

auto clr_assembly::get_type(const std::string& full_or_simple_name) const -> clr_type
{
	if(!valid())
	{
		return {};
	}

	auto handle = bridge().assembly_get_type(assembly_, full_or_simple_name.c_str());
	return handle ? clr_type(handle) : clr_type();
}

auto clr_assembly::get_type(const std::string& name_space, const std::string& name) const -> clr_type
{
	if(name_space.empty())
	{
		return get_type(name);
	}
	return get_type(name_space + "." + name);
}

auto clr_assembly::get_types() const -> std::vector<clr_type>
{
	if(!valid())
	{
		return {};
	}

	return fetch_and_map<clr_type>(
		[this](clr_handle* buffer, int32_t count)
		{ return bridge().assembly_get_types(assembly_, buffer, count); },
		[](clr_handle handle) { return clr_type(handle); });
}

auto clr_assembly::get_types_derived_from(const clr_type& base) const -> std::vector<clr_type>
{
	if(!valid() || !base.valid())
	{
		return {};
	}

	return fetch_and_map<clr_type>(
		[this, &base](clr_handle* buffer, int32_t count)
		{
			return bridge().assembly_get_types_derived_from(assembly_, base.get_internal_ptr(), buffer, count);
		},
		[](clr_handle handle) { return clr_type(handle); });
}

auto clr_assembly::get_corlib() -> clr_assembly
{
	return clr_assembly(bridge().assembly_get_corlib());
}

auto clr_assembly::dump_references() const -> std::vector<std::string>
{
	std::vector<std::string> result;
	if(!valid())
	{
		return result;
	}

	auto joined = take_string(bridge().assembly_dump_references(assembly_));
	std::istringstream stream(joined);
	std::string line;
	while(std::getline(stream, line))
	{
		if(!line.empty())
		{
			result.push_back(line);
		}
	}
	return result;
}

auto clr_assembly::get_internal_ptr() const -> clr_handle
{
	return assembly_;
}

} // namespace clr
