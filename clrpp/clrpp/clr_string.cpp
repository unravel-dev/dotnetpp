#include "clr_string.h"
#include "clr_domain.h"

namespace clr
{

namespace
{

// Minimal utf8 decoding for the utf16/utf32 accessors.
auto decode_utf8(const std::string& utf8) -> std::u32string
{
	std::u32string result;
	result.reserve(utf8.size());

	size_t i = 0;
	while(i < utf8.size())
	{
		unsigned char c = static_cast<unsigned char>(utf8[i]);
		char32_t cp = 0;
		size_t extra = 0;

		if(c < 0x80)
		{
			cp = c;
		}
		else if((c & 0xE0) == 0xC0)
		{
			cp = c & 0x1F;
			extra = 1;
		}
		else if((c & 0xF0) == 0xE0)
		{
			cp = c & 0x0F;
			extra = 2;
		}
		else if((c & 0xF8) == 0xF0)
		{
			cp = c & 0x07;
			extra = 3;
		}
		else
		{
			++i;
			continue; // invalid byte
		}

		if(i + extra >= utf8.size() + 1)
		{
			break;
		}

		bool ok = true;
		for(size_t k = 1; k <= extra; ++k)
		{
			unsigned char cont = static_cast<unsigned char>(utf8[i + k]);
			if((cont & 0xC0) != 0x80)
			{
				ok = false;
				break;
			}
			cp = (cp << 6) | (cont & 0x3F);
		}

		if(!ok)
		{
			++i;
			continue;
		}

		result.push_back(cp);
		i += extra + 1;
	}

	return result;
}

} // namespace

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
	auto utf32 = as_utf32();
	std::u16string result;
	result.reserve(utf32.size());
	for(char32_t cp : utf32)
	{
		if(cp <= 0xFFFF)
		{
			result.push_back(static_cast<char16_t>(cp));
		}
		else
		{
			cp -= 0x10000;
			result.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
			result.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
		}
	}
	return result;
}

auto clr_string::as_utf32() const -> std::u32string
{
	return decode_utf8(as_utf8());
}

} // namespace clr
