#pragma once

#include "mono_config.h"

#include "mono_type.h"
#include "mono_visibility.h"

BEGIN_MONO_INCLUDE
#include <mono/metadata/object.h>
END_MONO_INCLUDE

namespace mono
{

class mono_object;

class mono_property
{
public:
	struct meta_info;

	explicit mono_property(const mono_type& type, const std::string& name);

	auto get_name() const -> std::string;

	auto get_fullname() const -> std::string;

	auto get_full_declname() const -> std::string;

	auto get_type() const -> const mono_type&;

	auto get_get_method() const -> mono_method;

	auto get_set_method() const -> mono_method;

	auto get_visibility() const -> visibility;

	auto is_static() const -> bool;

	auto is_readonly() const -> bool;

	auto get_attributes() const -> std::vector<mono_object>;

	auto has_attribute_fullname(const std::string& attribute_full_name) const -> bool;

	auto has_attribute(const std::string& attribute_name) const -> bool;

	auto get_attribute(const std::string& attribute_name) const -> mono_object;

	auto get_attribute_fullname(const std::string& attribute_full_name) const -> mono_object;

	auto is_special_name() const -> bool;

	auto has_default() const -> bool;

	auto is_valid() const -> bool;

	/// True if both wrappers refer to the same MonoProperty.
	auto equals(const mono_property& other) const -> bool;

	friend auto operator==(const mono_property& a, const mono_property& b) -> bool
	{
		return a.equals(b);
	}

	friend auto operator!=(const mono_property& a, const mono_property& b) -> bool
	{
		return !a.equals(b);
	}

	/// Backend pointer. Prefer is_valid() / equals() over raw pointer checks.
	auto get_internal_ptr() const -> MonoProperty*;

private:
	void generate_meta();

	mono_type type_;

	non_owning_ptr<MonoProperty> property_ = nullptr;

	std::shared_ptr<meta_info> meta_{};
};

void reset_property_cache();

} // namespace mono
