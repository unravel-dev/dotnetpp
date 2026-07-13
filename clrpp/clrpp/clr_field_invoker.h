#pragma once
#include "clr_field.h"

#include "clr_method_invoker.h"
#include "clr_object.h"
#include "clr_type_conversion.h"

namespace clr
{

template <typename T>
class clr_field_invoker : public clr_field
{
public:
	using value_type = T;

	void set_value(const T& val) const;

	void set_value(const clr_object& obj, const T& val) const;

	auto get_value() const -> T;

	auto get_value(const clr_object& obj) const -> T;

private:
	template <typename signature_t>
	friend auto make_field_invoker(const clr_field&) -> clr_field_invoker<signature_t>;

	explicit clr_field_invoker(const clr_field& field)
		: clr_field(field)
	{
	}

	void set_value_impl(const clr_object* obj, const T& val) const;

	auto get_value_impl(const clr_object* obj) const -> T;
};

template <typename T>
void clr_field_invoker<T>::set_value(const T& val) const
{
	set_value_impl(nullptr, val);
}

template <typename T>
void clr_field_invoker<T>::set_value(const clr_object& object, const T& val) const
{
	set_value_impl(&object, val);
}

template <typename T>
void clr_field_invoker<T>::set_value_impl(const clr_object* object, const T& val) const
{
	assert(field_);

	auto managed_val = clr_converter<T>::to_mono(val);
	auto variant = to_clr_variant(managed_val);

	clr_handle target = (object && object->valid()) ? object->get_internal_ptr() : nullptr;

	clr_exception_info_raw ex{};
	bridge().field_set_value(field_, target, &variant, &ex);
	throw_if_exception(ex);
}

template <typename T>
auto clr_field_invoker<T>::get_value() const -> T
{
	return get_value_impl(nullptr);
}

template <typename T>
auto clr_field_invoker<T>::get_value(const clr_object& object) const -> T
{
	return get_value_impl(&object);
}

template <typename T>
auto clr_field_invoker<T>::get_value_impl(const clr_object* object) const -> T
{
	assert(field_);

	using result_traits = detail::invoke_result<T>;
	typename result_traits::storage_type storage{};
	clr_variant result = result_traits::prepare(storage);

	clr_handle target = (object && object->valid()) ? object->get_internal_ptr() : nullptr;

	clr_exception_info_raw ex{};
	bridge().field_get_value(field_, target, &result, &ex);
	throw_if_exception(ex);

	return result_traits::extract(storage, result);
}

template <typename T>
auto make_field_invoker(const clr_field& field) -> clr_field_invoker<T>
{
	return clr_field_invoker<T>(field);
}

template <typename T>
auto make_field_invoker(const clr_type& type, const std::string& name) -> clr_field_invoker<T>
{
	auto field = type.get_field(name);
	return make_field_invoker<T>(field);
}

template <typename T>
auto make_invoker(const clr_field& field) -> clr_field_invoker<T>
{
	return make_field_invoker<T>(field);
}

template <typename T>
auto set_field_value(const clr_object& obj, const std::string& name, const T& val) -> bool
{
	try
	{
		auto invoker = make_field_invoker<T>(obj.get_type(), name);
		invoker.set_value(obj, val);
		return true;
	}
	catch(const std::exception&)
	{
		return false;
	}
}

template <typename T>
auto set_field_value(const clr_type& type, const std::string& name, const T& val) -> bool
{
	try
	{
		auto invoker = make_field_invoker<T>(type, name);
		invoker.set_value(val);
		return true;
	}
	catch(const std::exception&)
	{
		return false;
	}
}

template <typename T>
auto get_field_value(const clr_object& obj, const std::string& name, T& val) -> bool
{
	try
	{
		auto invoker = make_field_invoker<T>(obj.get_type(), name);
		val = invoker.get_value(obj);
		return true;
	}
	catch(const std::exception&)
	{
		return false;
	}
}

template <typename T>
auto get_field_value(const clr_type& type, const std::string& name, T& val) -> bool
{
	try
	{
		auto invoker = make_field_invoker<T>(type, name);
		val = invoker.get_value();
		return true;
	}
	catch(const std::exception&)
	{
		return false;
	}
}

} // namespace clr
