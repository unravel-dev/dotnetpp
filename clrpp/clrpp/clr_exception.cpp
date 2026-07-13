#include "clr_exception.h"

#include <regex>
#include <sstream>

namespace clr
{

clr_thunk_exception::clr_thunk_exception(const clr_exception_info& info)
	: clr_exception(info.exception_typename + "(" + info.message + ")\n" + info.stacktrace)
	, info_(info)
{
}

auto clr_thunk_exception::exception_typename() const -> const std::string&
{
	return info_.exception_typename;
}

auto clr_thunk_exception::message() const -> const std::string&
{
	return info_.message;
}

auto clr_thunk_exception::soruce() const -> const std::string&
{
	return info_.source;
}

auto clr_thunk_exception::stacktrace() const -> const std::string&
{
	return info_.stacktrace;
}

auto make_thunk_exception(clr_exception_info_raw& raw) -> clr_thunk_exception
{
	clr_thunk_exception::clr_exception_info info;
	info.exception_typename = take_string(raw.type_name);
	info.message = take_string(raw.message);
	info.source = take_string(raw.source);
	info.stacktrace = take_string(raw.stack_trace);
	raw = {};
	return clr_thunk_exception(info);
}

void throw_if_exception(clr_exception_info_raw& raw)
{
	if(raw.has_value != 0)
	{
		throw make_thunk_exception(raw);
	}
}

namespace
{
thread_local std::string pending_exception_payload;
thread_local bool pending_exception_set = false;
} // namespace

void raise_exception(const std::string& name_space, const std::string& class_name, const std::string& message)
{
	pending_exception_payload = name_space + "|" + class_name + "|" + message;
	pending_exception_set = true;
}

auto consume_pending_exception() -> const char*
{
	if(!pending_exception_set)
	{
		return nullptr;
	}

	pending_exception_set = false;
	return pending_exception_payload.c_str();
}

auto extract_relevant_stack_frame(const std::string& input) -> stack_frame_info
{
	std::regex cs_regex(R"(([^\s]+\.cs):(?:line )?(\d+))");
	std::smatch match;

	std::istringstream iss(input);
	std::string line;

	stack_frame_info result{};

	while(std::getline(iss, line))
	{
		if(std::regex_search(line, match, cs_regex))
		{
			result.file_name = match[1].str();
			result.line = std::stoi(match[2].str());
			break;
		}
	}

	return result;
}

} // namespace clr
