#include "mono_exception.h"
#include "mono_object.h"
#include "mono_property_invoker.h"
#include "mono_type.h"

#include <regex>
#include <sstream>

BEGIN_MONO_INCLUDE
#include <mono/metadata/exception.h>
END_MONO_INCLUDE

namespace mono
{

auto get_exception_info(MonoObject* ex) -> mono_thunk_exception::mono_exception_info
{
	auto obj = mono_object(ex);
	const auto& type = obj.get_type();
	const auto& exception_typename = type.get_fullname();

	auto source_prop = type.get_property("Source");
	auto mutable_source_prop = make_property_invoker<std::string>(source_prop);

	auto message_prop = type.get_property("Message");
	auto mutable_message_prop = make_property_invoker<std::string>(message_prop);

	auto stacktrace_prop = type.get_property("StackTrace");
	auto mutable_stack_prop = make_property_invoker<std::string>(stacktrace_prop);

	auto source = mutable_source_prop.get_value(obj);
	auto message = mutable_message_prop.get_value(obj);
	auto stacktrace = mutable_stack_prop.get_value(obj);
	return {exception_typename, message, source, stacktrace};
}

mono_thunk_exception::mono_thunk_exception(MonoObject* ex)
	: mono_thunk_exception(get_exception_info(ex))
{
}

auto mono_thunk_exception::exception_typename() const -> const std::string&
{
	return info_.exception_typename;
}

auto mono_thunk_exception::message() const -> const std::string&
{
	return info_.message;
}

auto mono_thunk_exception::source() const -> const std::string&
{
	return info_.source;
}

auto mono_thunk_exception::stacktrace() const -> const std::string&
{
	return info_.stacktrace;
}

mono_thunk_exception::mono_thunk_exception(const mono_exception_info& info)
	: mono_exception(info.exception_typename + "(" + info.message + ")\n" + info.stacktrace)
	, info_(info)
{
}

void raise_exception(const std::string& name_space, const std::string& class_name, const std::string& message)
{
	// Create a managed exception: System.InvalidOperationException
	MonoException* exception =
		mono_exception_from_name_msg(mono_get_corlib(),
									 name_space.c_str(), class_name.c_str(), message.c_str());

	// Raise the exception in the managed runtime
	mono_raise_exception(exception);
}

auto extract_relevant_stack_frame(const std::string& input) -> stack_frame_info
{
	// Regex to capture:
	//   1) the file path ending with `.cs`
	//   2) the line number after the colon
	// Explanation of the pattern:
	//   ([^\s]+\.cs)  -> matches non-whitespace until ".cs"
	//   :(\d+)        -> a literal colon, then one or more digits
	std::regex cs_regex(R"(([^\s]+\.cs):(\d+))");
	std::smatch match;

	std::istringstream iss(input);
	std::string line;

	// We'll store the first file/line match we find and then stop.
	stack_frame_info result{};

	while(std::getline(iss, line))
	{
		if(std::regex_search(line, match, cs_regex))
		{
			// match[1] is the .cs file path, match[2] is the line number
			result.file_name = match[1].str();
			result.line = std::stoi(match[2].str());
			// Since we want the FIRST occurrence (topmost),
			// we break immediately after finding it.
			break;
		}
	}

	return result;
}

} // namespace mono
