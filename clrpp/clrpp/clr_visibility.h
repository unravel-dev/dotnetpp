#pragma once

#include "clr_config.h"

namespace clr
{
enum class visibility
{
	vis_private,
	vis_protected_internal,
	vis_internal,
	vis_protected,
	vis_public
};

inline auto to_string(visibility vis) -> std::string
{
	switch(vis)
	{
		case visibility::vis_private:
			return "private";
		case visibility::vis_protected_internal:
			return "protected internal";
		case visibility::vis_internal:
			return "internal";
		case visibility::vis_protected:
			return "protected";
		case visibility::vis_public:
			return "public";
	}
	return "private";
}

inline auto visibility_from_flags(int32_t flags) -> visibility
{
	switch((flags >> 8) & 0x7)
	{
		case 0:
			return visibility::vis_private;
		case 1:
			return visibility::vis_protected_internal;
		case 2:
			return visibility::vis_internal;
		case 3:
			return visibility::vis_protected;
		case 4:
			return visibility::vis_public;
		default:
			return visibility::vis_private;
	}
}

} // namespace clr
