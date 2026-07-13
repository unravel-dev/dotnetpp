#pragma once

#include "clr_bridge.h"
#include "clr_config.h"
#include "clr_member_meta.h"
#include "clr_type.h"
#include "clr_visibility.h"

namespace clr
{

class clr_object;
class clr_method;

class clr_property
{
public:
	struct meta_info;

	explicit clr_property(clr_handle property_handle);
	explicit clr_property(const clr_type& type, const std::string& name);

	auto get_name() const -> std::string;

	auto get_fullname() const -> std::string;

	auto get_full_declname() const -> std::string;

	auto get_type() const -> const clr_type&;

	auto get_get_method() const -> clr_method;

	auto get_set_method() const -> clr_method;

	auto get_visibility() const -> visibility;

	auto is_static() const -> bool;

	auto is_readonly() const -> bool;

	auto get_attributes() const -> std::vector<clr_object>;

	auto has_attribute_fullname(const std::string& attribute_full_name) const -> bool;

	auto has_attribute(const std::string& attribute_name) const -> bool;

	auto get_attribute(const std::string& attribute_name) const -> clr_object;

	auto get_attribute_fullname(const std::string& attribute_full_name) const -> clr_object;

	auto is_special_name() const -> bool;

	auto has_default() const -> bool;

	auto get_internal_ptr() const -> clr_handle;

private:
	void generate_meta(const clr_type& declaring_type);

	clr_type type_;

	non_owning_ptr<void> property_ = nullptr;

	std::shared_ptr<meta_info> meta_{};
};

struct clr_property::meta_info : clr_member_meta_info
{
};

void reset_property_cache();

} // namespace clr
