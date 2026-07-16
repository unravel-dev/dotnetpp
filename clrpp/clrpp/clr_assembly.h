#pragma once

#include "clr_bridge.h"
#include "clr_config.h"
#include "clr_type.h"

namespace clr
{

class clr_domain;

class clr_assembly
{
public:
	clr_assembly() = default;
	explicit clr_assembly(const clr_domain& domain, const std::string& path, bool shared = true);
	explicit clr_assembly(clr_handle assembly_handle);

	auto valid() const -> bool;

	auto get_type(const std::string& full_or_simple_name) const -> clr_type;
	auto get_type(const std::string& name_space, const std::string& name) const -> clr_type;

	auto get_types() const -> std::vector<clr_type>;
	auto get_types_derived_from(const clr_type& base) const -> std::vector<clr_type>;

	static auto get_corlib() -> clr_assembly;
	auto dump_references() const -> std::vector<std::string>;

	/// True if both wrappers refer to the same Assembly.
	auto equals(const clr_assembly& other) const -> bool;

	friend auto operator==(const clr_assembly& a, const clr_assembly& b) -> bool
	{
		return a.equals(b);
	}

	friend auto operator!=(const clr_assembly& a, const clr_assembly& b) -> bool
	{
		return !a.equals(b);
	}

	/// Backend handle. Prefer equals() for identity checks.
	auto get_internal_ptr() const -> clr_handle;

private:
	non_owning_ptr<void> assembly_ = nullptr;
};

} // namespace clr
