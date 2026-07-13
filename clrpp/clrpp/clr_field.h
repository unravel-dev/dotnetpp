#pragma once

#include "clr_bridge.h"
#include "clr_config.h"
#include "clr_type.h"
#include "clr_visibility.h"

namespace clr
{
class clr_object;

class clr_field
{
public:
	struct meta_info;

	explicit clr_field(clr_handle field_handle);
	explicit clr_field(const clr_type& type, const std::string& name);

	auto get_name() const -> std::string;

	auto get_fullname() const -> std::string;

	auto get_full_declname() const -> std::string;

	auto get_type() const -> const clr_type&;

	auto get_visibility() const -> visibility;

	auto is_static() const -> bool;

	auto get_attributes() const -> std::vector<clr_object>;

	auto has_attribute_fullname(const std::string& attribute_full_name) const -> bool;

	auto has_attribute(const std::string& attribute_name) const -> bool;

	auto get_attribute_fullname(const std::string& attribute_full_name) const -> clr_object;

	auto get_attribute(const std::string& attribute_name) const -> clr_object;

	auto is_readonly() const -> bool;

	auto is_const() const -> bool;

	auto is_backing_field() const -> bool;

	auto get_internal_ptr() const -> clr_handle;

protected:
	void generate_meta(const clr_type& declaring_type);

	auto is_valuetype() const -> bool;

	clr_type type_;

	non_owning_ptr<void> field_ = nullptr;

	std::shared_ptr<meta_info> meta_{};
};

struct clr_field::meta_info
{
	std::string name;
	std::string fullname;
	std::string full_declname;
	int32_t flags = 0;
};

void reset_field_cache();

} // namespace clr
