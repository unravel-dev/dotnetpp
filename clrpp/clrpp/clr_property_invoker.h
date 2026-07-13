#pragma once
#include "clr_property.h"

#include "clr_method_invoker.h"

namespace clr
{

template <typename T>
class clr_property_invoker : public clr_property
{
public:
	using value_type = T;

	void set_value(const T& val) const;

	void set_value(const clr_object& obj, const T& val) const;

	auto get_value() const -> T;

	auto get_value(const clr_object& obj) const -> T;

	template <typename IndexArg>
	auto get_value_with_args(IndexArg index) const -> T;

	template <typename IndexArg>
	auto get_value_with_args(const clr_object& obj, IndexArg index) const -> T;

	template <typename IndexArg>
	void set_value_with_args(IndexArg index, const T& val) const;

	template <typename IndexArg>
	void set_value_with_args(const clr_object& obj, IndexArg index, const T& val) const;

private:
	template <typename Signature>
	friend auto make_property_invoker(const clr_property&) -> clr_property_invoker<Signature>;

	explicit clr_property_invoker(const clr_property& property)
		: clr_property(property)
	{
	}
};

template <typename T>
void clr_property_invoker<T>::set_value(const T& val) const
{
	auto thunk = make_method_invoker<void(const T&)>(get_set_method());
	thunk(val);
}

template <typename T>
void clr_property_invoker<T>::set_value(const clr_object& object, const T& val) const
{
	auto thunk = make_method_invoker<void(const T&)>(get_set_method());
	thunk(object, val);
}

template <typename T>
auto clr_property_invoker<T>::get_value() const -> T
{
	auto thunk = make_method_invoker<T()>(get_get_method());
	return thunk();
}

template <typename T>
auto clr_property_invoker<T>::get_value(const clr_object& object) const -> T
{
	auto thunk = make_method_invoker<T()>(get_get_method());
	return thunk(object);
}

template <typename T>
template <typename IndexArg>
auto clr_property_invoker<T>::get_value_with_args(IndexArg index) const -> T
{
	auto thunk = make_method_invoker<T(IndexArg)>(get_get_method());
	return thunk(index);
}

template <typename T>
template <typename IndexArg>
auto clr_property_invoker<T>::get_value_with_args(const clr_object& object, IndexArg index) const -> T
{
	auto thunk = make_method_invoker<T(IndexArg)>(get_get_method());
	return thunk(object, index);
}

template <typename T>
template <typename IndexArg>
void clr_property_invoker<T>::set_value_with_args(IndexArg index, const T& val) const
{
	auto thunk = make_method_invoker<void(IndexArg, const T&)>(get_set_method());
	thunk(index, val);
}

template <typename T>
template <typename IndexArg>
void clr_property_invoker<T>::set_value_with_args(const clr_object& object, IndexArg index,
												  const T& val) const
{
	auto thunk = make_method_invoker<void(IndexArg, const T&)>(get_set_method());
	thunk(object, index, val);
}

template <typename T>
auto make_property_invoker(const clr_property& property) -> clr_property_invoker<T>
{
	return clr_property_invoker<T>(property);
}

template <typename T>
auto make_property_invoker(const clr_type& type, const std::string& name) -> clr_property_invoker<T>
{
	auto property = type.get_property(name);
	return make_property_invoker<T>(property);
}

template <typename T>
auto make_invoker(const clr_property& property) -> clr_property_invoker<T>
{
	return make_property_invoker<T>(property);
}

} // namespace clr
