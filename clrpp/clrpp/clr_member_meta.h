#pragma once

#include "clr_config.h"

#include <string>

namespace clr
{

struct clr_member_meta_info
{
	std::string name;
	std::string fullname;
	std::string full_declname;
	int32_t flags = 0;
};

} // namespace clr
