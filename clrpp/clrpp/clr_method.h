#pragma once

#include "clr_bridge.h"
#include "clr_config.h"
#include "clr_member_meta.h"
#include "clr_type.h"
#include "clr_visibility.h"

namespace clr
{

class clr_method
{
public:
	struct meta_info;

	clr_method() = default;
	explicit clr_method(clr_handle method_handle);
	explicit clr_method(const clr_type& type, const std::string& name_with_args);
	explicit clr_method(const clr_type& type, const std::string& name, int argc);

	auto get_return_type() const -> clr_type;

	auto get_param_types() const -> const std::vector<clr_type>&;

	auto get_name() const -> std::string;

	auto get_fullname() const -> std::string;

	auto get_full_declname() const -> std::string;

	auto get_visibility() const -> visibility;

	auto is_static() const -> bool;

	auto is_virtual() const -> bool;

	auto is_pinvoke_impl() const -> bool;

	auto is_special_name() const -> bool;

	auto is_internal_call() const -> bool;

	auto is_synchronized() const -> bool;

	auto get_attributes() const -> std::vector<clr_type>;

	auto valid() const -> bool;
	operator bool() const;

	auto get_internal_ptr() const -> clr_handle;

protected:
	void generate_meta();
	void cache_param_types() const;

	non_owning_ptr<void> method_ = nullptr;

	mutable std::vector<clr_type> cached_param_types_;
	mutable bool param_types_cached_ = false;

	std::shared_ptr<meta_info> meta_{};
};

struct clr_method::meta_info : clr_member_meta_info
{
};

void reset_method_cache();

} // namespace clr
