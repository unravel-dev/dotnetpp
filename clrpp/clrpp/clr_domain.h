#pragma once

#include "clr_assembly.h"
#include "clr_bridge.h"
#include "clr_config.h"

#include <map>

namespace clr
{

class clr_string;

/*
 * Domain backed by a collectible AssemblyLoadContext. Destroying the domain
 * unloads the context (best effort - collectible ALC unloading is async and
 * requires all managed references to be released).
 */
class clr_domain
{
public:
	explicit clr_domain(const std::string& name);
	~clr_domain();

	clr_domain(const clr_domain&) = delete;
	auto operator=(const clr_domain&) -> clr_domain& = delete;

	auto get_assembly(const std::string& path, bool shared = true) const -> clr_assembly;

	auto get_type(const std::string& name) const -> clr_type;
	auto get_type(const std::string& name_space, const std::string& name) const -> clr_type;

	auto new_string(const std::string& str) const -> clr_string;

	auto get_name() const -> std::string;

	auto get_version() const -> uint64_t;

	/// True if both wrappers refer to the same load context / domain.
	auto equals(const clr_domain& other) const -> bool;

	friend auto operator==(const clr_domain& a, const clr_domain& b) -> bool
	{
		return a.equals(b);
	}

	friend auto operator!=(const clr_domain& a, const clr_domain& b) -> bool
	{
		return !a.equals(b);
	}

	/// Backend handle. Prefer equals() for identity checks.
	auto get_internal_ptr() const -> clr_handle;

	static void set_current_domain(const clr_domain& domain);
	static void set_current_domain(const clr_domain* domain);
	static auto get_current_domain() -> const clr_domain&;
	/// Non-throwing; nullptr when no domain is current.
	static auto get_current_domain_ptr() -> const clr_domain*;
	static void set_assemblies_path(const std::string& path);

private:
	clr_handle domain_ = nullptr;
	std::string name_;

	mutable std::map<std::string, clr_assembly> assemblies_;
};

} // namespace clr
