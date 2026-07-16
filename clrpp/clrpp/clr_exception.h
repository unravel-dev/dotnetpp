#pragma once

#include "clr_bridge.h"
#include "clr_config.h"

namespace clr
{

class clr_exception : public std::runtime_error
{
	using runtime_error::runtime_error;
};

class clr_thunk_exception : public clr_exception
{
public:
	struct clr_exception_info
	{
		std::string exception_typename;
		std::string message;
		std::string source;
		std::string stacktrace;
	};

	explicit clr_thunk_exception(const clr_exception_info& info);

	auto exception_typename() const -> const std::string&;

	auto message() const -> const std::string&;

	auto source() const -> const std::string&;

	auto stacktrace() const -> const std::string&;

private:
	clr_exception_info info_;
};

/// Convert bridge exception info (consuming its strings) into a thunk exception.
auto make_thunk_exception(clr_exception_info_raw& raw) -> clr_thunk_exception;

/// Throws clr_thunk_exception if raw carries an exception.
void throw_if_exception(clr_exception_info_raw& raw);

/*
 * Set the pending managed exception for the current thread. Unlike mono,
 * CoreCLR cannot raise a managed exception from inside a native internal
 * call; instead the exception is queued here and (re)thrown by the managed
 * caller right after the internal call returns (see Clrpp.InternalCalls).
 */
void raise_exception(const std::string& name_space, const std::string& class_name, const std::string& message);

/// Used by the bridge bootstrap: returns pending "ns|class|message" or null and clears it.
auto consume_pending_exception() -> const char*;

struct stack_frame_info
{
	std::string function_name{};
	std::string file_name{};
	int line{};
};

auto extract_relevant_stack_frame(const std::string& input) -> stack_frame_info;

} // namespace clr
