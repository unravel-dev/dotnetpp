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

auto find_handler(const std::string& category) -> const log_handler*
{
	auto& handlers = get_handlers();
	auto it = handlers.find(category);
	return it != handlers.end() ? &it->second : nullptr;
}
} // namespace

void set_log_handler(const std::string& category, const log_handler& handler)
{
	get_handlers()[category] = handler;
}

auto get_log_handler(const std::string& category) -> const log_handler&
{
	static const log_handler empty{};
	auto* handler = find_handler(category);
	return handler ? *handler : empty;
}

void log_message(const std::string& message, const std::string& category)
{
	const auto* handler = find_handler(category);
	if(handler && *handler)
	{
		(*handler)(message);
		return;
	}

	static const std::string default_category{"default"};
	const auto* default_handler = find_handler(default_category);
	if(default_handler && *default_handler)
	{
		(*default_handler)(message);
	}
}

} // namespace clr
