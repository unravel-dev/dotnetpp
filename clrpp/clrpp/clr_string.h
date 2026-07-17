#pragma once

#include "clr_config.h"
#include "clr_object.h"

namespace clr
{
class clr_domain;

class clr_string : public clr_object
{
public:
	explicit clr_string(const clr_object& obj);
	explicit clr_string(const clr_domain& domain, const std::string& as_utf8);

	auto as_utf8() const -> std::string;
	auto as_utf16() const -> std::u16string;
	auto as_utf32() const -> std::u32string;
};

} // namespace clr
