#pragma once

#include "clr_bridge.h"
#include "clr_config.h"

namespace clr
{

class clr_assembly;
class clr_method;
class clr_field;
class clr_property;
class clr_object;
class clr_domain;

class clr_type
{
public:
	struct meta_info;

	clr_type();

	/// Wrap an interned System.Type handle.
	explicit clr_type(clr_handle type_handle);

	auto valid() const -> bool;

	auto new_instance() const -> clr_object;
	auto new_instance(const clr_domain& domain) const -> clr_object;

	auto get_method(const std::string& name_with_args) const -> clr_method;

	auto get_method(const std::string& name, int argc) const -> clr_method;

	auto get_field(const std::string& name) const -> clr_field;

	auto get_property(const std::string& name) const -> clr_property;

	auto get_fields(bool include_base = false) const -> std::vector<clr_field>;

	auto get_properties(bool include_base = false) const -> std::vector<clr_property>;

	auto get_methods(bool include_base = false) const -> std::vector<clr_method>;

	auto get_attributes(bool include_base = false) const -> std::vector<clr_object>;

	auto has_base_type() const -> bool;

	auto get_base_type() const -> clr_type;

	auto get_nested_types() const -> std::vector<clr_type>;

	auto get_nesting_type() const -> clr_type;

	auto is_derived_from(const clr_type& type) const -> bool;

	auto get_namespace() const -> std::string;

	auto get_name() const -> std::string;

	auto get_hash() const -> size_t;

	auto get_fullname() const -> std::string;

	auto is_valuetype() const -> bool;

	auto is_struct() const -> bool;

	auto is_class() const -> bool;

	auto is_enum() const -> bool;

	auto get_enum_base_type() const -> clr_type;

	template <typename T>
	auto get_enum_values() const -> std::vector<std::pair<T, std::string>>;

	auto get_rank() const -> int;

	auto is_array() const -> bool;

	auto get_element_type() const -> clr_type;

	auto get_sizeof() const -> std::uint32_t;

	auto get_alignof() const -> std::uint32_t;

	auto is_abstract() const -> bool;
	auto is_sealed() const -> bool;
	auto is_interface() const -> bool;
	auto is_serializable() const -> bool;
	auto is_string() const -> bool;
	auto is_list() const -> bool;

	/// Type of List<this>.
	auto get_list_type() const -> clr_type;

	/// True if both wrappers refer to the same System.Type instance.
	auto equals(const clr_type& other) const -> bool;

	friend auto operator==(const clr_type& a, const clr_type& b) -> bool
	{
		return a.equals(b);
	}

	friend auto operator!=(const clr_type& a, const clr_type& b) -> bool
	{
		return !a.equals(b);
	}

	/// Backend handle (interned for types). Prefer equals() for identity.
	auto get_internal_ptr() const -> clr_handle;

	static auto get_hash(const std::string& name) -> size_t;
	static auto get_hash(const char* name) -> size_t;

private:
	void generate_meta();

	non_owning_ptr<void> type_ = nullptr;
	std::shared_ptr<meta_info> meta_{};
};

struct clr_type::meta_info
{
	std::string name_space;
	std::string name;
	std::string fullname;
	int32_t flags = 0;
};

template <typename T>
auto clr_type::get_enum_values() const -> std::vector<std::pair<T, std::string>>
{
	std::vector<std::pair<T, std::string>> result;
	if(!valid())
	{
		return result;
	}

	auto count = bridge().type_get_enum_values(type_, nullptr, nullptr, 0);
	if(count <= 0)
	{
		return result;
	}

	std::vector<int64_t> values(static_cast<size_t>(count));
	std::vector<const char*> names(static_cast<size_t>(count));
	bridge().type_get_enum_values(type_, values.data(), names.data(), count);

	result.reserve(static_cast<size_t>(count));
	for(int32_t i = 0; i < count; ++i)
	{
		result.emplace_back(static_cast<T>(values[static_cast<size_t>(i)]),
							take_string(names[static_cast<size_t>(i)]));
	}
	return result;
}

void reset_type_cache();

} // namespace clr
