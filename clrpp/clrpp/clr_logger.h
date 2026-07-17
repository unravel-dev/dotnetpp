#pragma once

#include "clr_config.h"
#include <functional>

namespace clr
{
using log_handler = std::function<void(const std::string&)>;
void set_log_handler(const std::string& category, const log_handler& handler);
auto get_log_handler(const std::string& category) -> const log_handler&;
void log_message(const std::string& message, const std::string& category = "default");
} // namespace clr
