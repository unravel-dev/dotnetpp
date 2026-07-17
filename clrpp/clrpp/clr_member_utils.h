#pragma once

#include "clr_bridge.h"
#include "clr_bridge_utils.h"
#include "clr_member_flags.h"
#include "clr_object.h"
#include "clr_visibility.h"

#include <string>
#include <vector>

namespace clr
{

inline auto make_member_full_declname(int32_t flags, const std::string& fullname) -> std::string
{
	std::string storage = has_member_flag_static(flags) ? " static " : " ";
	return to_string(visibility_from_flags(flags)) + storage + fullname;
}

inline auto find_attribute_by_name(const std::vector<clr_object>& attributes, const std::string& attribute_name)
	-> clr_object
{
	for(const auto& attr : attributes)
	{
		if(attr.get_type().get_name() == attribute_name)
		{
			return attr;
		}
	}
	return {};
}

inline auto find_attribute_by_fullname(const std::vector<clr_object>& attributes,
									   const std::string& attribute_full_name) -> clr_object
{
	for(const auto& attr : attributes)
	{
		if(attr.get_type().get_fullname() == attribute_full_name)
		{
			return attr;
		}
	}
	return {};
}

inline auto has_attribute_by_name(const std::vector<clr_object>& attributes, const std::string& attribute_name)
	-> bool
{
	return find_attribute_by_name(attributes, attribute_name).valid();
}

inline auto has_attribute_by_fullname(const std::vector<clr_object>& attributes,
									  const std::string& attribute_full_name) -> bool
{
	return find_attribute_by_fullname(attributes, attribute_full_name).valid();
}

template<typename FillFn>
auto fetch_managed_objects(FillFn fill) -> std::vector<clr_object>
{
	return fetch_and_map<clr_object>(fill,
									 [](clr_handle handle) { return clr_object(managed_ptr::adopt(handle)); });
}

} // namespace clr
