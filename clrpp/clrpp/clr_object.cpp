#include "clr_object.h"
#include "clr_domain.h"
#include "clr_exception.h"

namespace clr
{

clr_object::clr_object() = default;

clr_object::clr_object(managed_ptr obj)
	: object_(std::move(obj))
{
	if(object_)
	{
		type_ = clr_type(bridge().object_get_type(object_.get()));
	}
}

clr_object::clr_object(managed_ptr obj, const clr_type& type)
	: type_(type)
	, object_(std::move(obj))
{
}

clr_object::clr_object(const clr_domain& domain, const clr_type& type)
{
	(void)domain;
	clr_exception_info_raw ex{};
	object_ = managed_ptr::adopt(bridge().object_create(type.get_internal_ptr(), &ex));
	throw_if_exception(ex);
	type_ = type;
}

auto clr_object::get_type() const -> const clr_type&
{
	return type_;
}

auto clr_object::valid() const -> bool
{
	return static_cast<bool>(object_);
}

clr_object::operator bool() const
{
	return valid();
}

auto clr_object::is_valid_clr_object() const -> bool
{
	return valid();
}

auto clr_object::get_internal_ptr() const -> clr_handle
{
	return object_.get();
}

auto clr_object::get_managed_ptr() const -> const managed_ptr&
{
	return object_;
}

} // namespace clr
