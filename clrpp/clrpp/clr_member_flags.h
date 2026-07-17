#pragma once

#include "clr_config.h"

namespace clr
{

enum member_flag_bits : int32_t
{
	member_flag_static = 1 << 0,
	member_flag_special_name = 1 << 3,
};

enum method_flag_bits : int32_t
{
	method_flag_static = member_flag_static,
	method_flag_virtual = 1 << 1,
	method_flag_pinvoke = 1 << 2,
	method_flag_special_name = member_flag_special_name,
	method_flag_internal_call = 1 << 4,
	method_flag_synchronized = 1 << 5,
};

enum property_flag_bits : int32_t
{
	property_flag_static = member_flag_static,
	property_flag_readonly = 1 << 1,
	property_flag_special_name = member_flag_special_name,
	property_flag_has_default = 1 << 4,
};

enum field_flag_bits : int32_t
{
	field_flag_static = member_flag_static,
	field_flag_readonly = 1 << 1,
	field_flag_const = 1 << 2,
	field_flag_backing = 1 << 3,
};

inline auto has_member_flag_static(int32_t flags) -> bool
{
	return (flags & member_flag_static) != 0;
}

} // namespace clr
