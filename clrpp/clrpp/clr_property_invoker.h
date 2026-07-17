#pragma once
#include "clr_property.h"

#include "clr_method_invoker.h"

#include <memory>

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

	auto get_invoker() const -> clr_method_invoker<T()>&
	{
		if(!get_invoker_)
		{
			get_invoker_ = std::make_shared<clr_method_invoker<T()>>(
				make_method_invoker<T()>(get_get_method()));
		}
		return *get_invoker_;
	}

	auto set_invoker() const -> clr_method_invoker<void(const T&)>&
	{
		if(!set_invoker_)
		{
			set_invoker_ = std::make_shared<clr_method_invoker<void(const T&)>>(
				make_method_invoker<void(const T&)>(get_set_method()));
		}
		return *set_invoker_;
	}

	auto get_indexed_invoker() const -> clr_method_invoker<T(int32_t)>&
	{
		if(!get_indexed_invoker_)
		{
			get_indexed_invoker_ = std::make_shared<clr_method_invoker<T(int32_t)>>(
				make_method_invoker<T(int32_t)>(get_get_method()));
		}
		return *get_indexed_invoker_;
	}

	auto set_indexed_invoker() const -> clr_method_invoker<void(int32_t, const T&)>&
	{
		if(!set_indexed_invoker_)
		{
			set_indexed_invoker_ = std::make_shared<clr_method_invoker<void(int32_t, const T&)>>(
				make_method_invoker<void(int32_t, const T&)>(get_set_method()));
		}
		return *set_indexed_invoker_;
	}

	mutable std::shared_ptr<clr_method_invoker<T()>> get_invoker_;
	mutable std::shared_ptr<clr_method_invoker<void(const T&)>> set_invoker_;
	mutable std::shared_ptr<clr_method_invoker<T(int32_t)>> get_indexed_invoker_;
	mutable std::shared_ptr<clr_method_invoker<void(int32_t, const T&)>> set_indexed_invoker_;
};

template <typename T>
void clr_property_invoker<T>::set_value(const T& val) const
{
	set_invoker()(val);
}

template <typename T>
void clr_property_invoker<T>::set_value(const clr_object& object, const T& val) const
{
	set_invoker()(object, val);
}

template <typename T>
auto clr_property_invoker<T>::get_value() const -> T
{
	return get_invoker()();
}

template <typename T>
auto clr_property_invoker<T>::get_value(const clr_object& object) const -> T
{
	return get_invoker()(object);
}

template <typename T>
template <typename IndexArg>
auto clr_property_invoker<T>::get_value_with_args(IndexArg index) const -> T
{
	return get_indexed_invoker()(static_cast<int32_t>(index));
}

template <typename T>
template <typename IndexArg>
auto clr_property_invoker<T>::get_value_with_args(const clr_object& object, IndexArg index) const -> T
{
	return get_indexed_invoker()(object, static_cast<int32_t>(index));
}

template <typename T>
template <typename IndexArg>
void clr_property_invoker<T>::set_value_with_args(IndexArg index, const T& val) const
{
	set_indexed_invoker()(static_cast<int32_t>(index), val);
}

template <typename T>
template <typename IndexArg>
void clr_property_invoker<T>::set_value_with_args(const clr_object& object, IndexArg index,
												  const T& val) const
{
	set_indexed_invoker()(object, static_cast<int32_t>(index), val);
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
