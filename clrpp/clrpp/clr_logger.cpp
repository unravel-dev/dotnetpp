#include "clr_logger.h"

#include <map>

namespace clr
{
namespace
{
auto get_handlers() -> std::map<std::string, log_handler>&
{
	static std::map<std::string, log_handler> handlers;
	return handlers;
}
} // namespace

void set_log_handler(const std::string& category, const log_handler& handler)
{
	get_handlers()[category] = handler;
}

auto get_log_handler(const std::string& category) -> const log_handler&
{
	return get_handlers()[category];
}

void log_message(const std::string& message, const std::string& category)
{
	const auto& handler = get_log_handler(category);
	if(handler)
	{
		handler(message);
	}
	else
	{
		const auto& default_handler = get_log_handler("default");
		if(default_handler)
		{
			default_handler(message);
		}
	}
}

} // namespace clr
