#pragma once

#include "clr_bridge.h"
#include "clr_config.h"
#include "clr_type.h"
#include "clr_type_traits.h"

namespace clr
{
class clr_domain;

class clr_object
{
public:
	clr_object();

	/// Wrap an owned (or shared) object handle.
	explicit clr_object(managed_ptr obj);
	explicit clr_object(managed_ptr obj, const clr_type& type);

	/// Create a new instance of the given type.
	explicit clr_object(const clr_domain& domain, const clr_type& type);

	auto get_type() const -> const clr_type&;

	auto valid() const -> bool;
	operator bool() const;

	auto is_valid_clr_object() const -> bool;

	/// True if both wrappers refer to the same managed instance
	/// (System.Object.ReferenceEquals), even when GCHandle values differ.
	auto equals(const clr_object& other) const -> bool;

	friend auto operator==(const clr_object& a, const clr_object& b) -> bool
	{
		return a.equals(b);
	}

	friend auto operator!=(const clr_object& a, const clr_object& b) -> bool
	{
		return !a.equals(b);
	}

	/// Backend handle. Prefer equals() / operator== for identity checks;
	/// handle values are not unique per managed instance.
	auto get_internal_ptr() const -> clr_handle;

	/// Shared ownership of the underlying handle.
	auto get_managed_ptr() const -> const managed_ptr&;

	template <typename T>
	void box_value(const T& value, const clr_type& type)
	{
		static_assert(is_clr_valuetype<T>::value, "Should not pass here for non-value types");
		object_ = managed_ptr::adopt(
			bridge().object_box(type.get_internal_ptr(), std::addressof(value), static_cast<int32_t>(sizeof(T))));
		type_ = type;
	}

	template <typename T>
	auto unbox_value() const -> T
	{
		static_assert(is_clr_valuetype<T>::value, "Should not pass here for non-value types");
		T value{};
		bridge().object_unbox(get_internal_ptr(), std::addressof(value), static_cast<int32_t>(sizeof(T)));
		return value;
	}

protected:
	clr_type type_;
	managed_ptr object_;
};

template <typename T>
auto clr_box_value(const T& value, const clr_type& type) -> clr_object
{
	static_assert(is_clr_valuetype<T>::value, "Should not pass here for non-value types");
	auto handle =
		bridge().object_box(type.get_internal_ptr(), std::addressof(value), static_cast<int32_t>(sizeof(T)));
	return clr_object(managed_ptr::adopt(handle), type);
}

template <typename T>
auto clr_unbox_value(const clr_object& obj) -> T
{
	static_assert(is_clr_valuetype<T>::value, "Should not pass here for non-value types");
	T value{};
	bridge().object_unbox(obj.get_internal_ptr(), std::addressof(value), static_cast<int32_t>(sizeof(T)));
	return value;
}

} // namespace clr
