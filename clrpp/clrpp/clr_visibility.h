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

} // namespace clr
