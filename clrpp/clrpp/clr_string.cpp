#include "clr_string.h"
#include "clr_domain.h"

#include <iterator>

#if defined(__clang__)
#pragma clang diagnostic push
#if defined(__has_warning) && __has_warning("-Wcharacter-conversion")
#pragma clang diagnostic ignored "-Wcharacter-conversion"
#endif
#endif
#include "utf8/unchecked.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace clr
{

clr_string::clr_string(const clr_object& obj)
	: clr_object(obj)
{
}

clr_string::clr_string(const clr_domain& domain, const std::string& as_utf8)
	: clr_object(managed_ptr::adopt(bridge().string_create(as_utf8.c_str())))
{
	(void)domain;
}

auto clr_string::as_utf8() const -> std::string
{
	if(!valid())
	{
		return {};
	}
	return take_string(bridge().string_get_utf8(get_internal_ptr()));
}

auto clr_string::as_utf16() const -> std::u16string
{
	auto utf8 = as_utf8();
	std::u16string result;
	result.reserve(utf8.size());
	utf8::unchecked::utf8to16(utf8.begin(), utf8.end(), std::back_inserter(result));
	return result;
}

auto clr_string::as_utf32() const -> std::u32string
{
	auto utf8 = as_utf8();
	std::u32string result;
	result.reserve(utf8.size());
	utf8::unchecked::utf8to32(utf8.begin(), utf8.end(), std::back_inserter(result));
	return result;
}

} // namespace clr
