#pragma once

#include "mono_config.h"

#include "mono_assembly.h"

BEGIN_MONO_INCLUDE
#include <mono/metadata/metadata.h>
END_MONO_INCLUDE

#include <unordered_map>

namespace mono
{
class mono_string;
class mono_type;

class mono_domain
{
public:
	explicit mono_domain(const std::string& name);

	~mono_domain();
	
	auto get_assembly(const std::string& path, bool shared = true) const -> mono_assembly;

	auto new_string(const std::string& str) const -> mono_string;

	static void set_current_domain(const mono_domain& domain);
	static void set_current_domain(const mono_domain* domain);
	static void set_assemblies_path(const std::string& path);

	static auto get_current_domain() -> const mono_domain&;

	auto get_type(const std::string& name) const -> mono_type;
	auto get_type(const std::string& name_space, const std::string& name) const -> mono_type;

	auto get_name() const -> std::string;

	auto get_version() const -> uint64_t;

	/// True if both wrappers refer to the same MonoDomain.
	auto equals(const mono_domain& other) const -> bool;

	friend auto operator==(const mono_domain& a, const mono_domain& b) -> bool
	{
		return a.equals(b);
	}

	friend auto operator!=(const mono_domain& a, const mono_domain& b) -> bool
	{
		return !a.equals(b);
	}

	/// Backend pointer. Prefer equals() for identity checks.
	auto get_internal_ptr() const -> MonoDomain*;

private:

	mutable std::unordered_map<std::string, mono_assembly> assemblies_;
	non_owning_ptr<MonoDomain> domain_ = nullptr;
};

} // namespace mono
