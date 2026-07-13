#pragma once

#include "clr_array.h"
#include "clr_config.h"
#include "clr_domain.h"
#include "clr_exception.h"
#include "clr_method_invoker.h"
#include "clr_object.h"
#include "clr_property_invoker.h"
#include "clr_type_traits.h"

#include <list>
#include <vector>

namespace clr
{

class clr_list_base : public clr_object
{
public:
	explicit clr_list_base(const clr_object& obj)
		: clr_object(obj)
	{
	}

	explicit clr_list_base(managed_ptr obj)
		: clr_object(std::move(obj))
	{
	}

	auto size() const -> std::size_t
	{
		auto invoker = make_method_invoker<int()>(get_type().get_method("get_Count", 0), false);
		return static_cast<std::size_t>(invoker(*this));
	}

	void clear()
	{
		auto invoker = make_method_invoker<void()>(get_type().get_method("Clear", 0), false);
		invoker(*this);
	}

	void remove_at(int32_t index)
	{
		auto invoker = make_method_invoker<void(int32_t)>(get_type().get_method("RemoveAt", 1), false);
		invoker(*this, index);
	}

	auto get_element_type() const -> clr_type
	{
		return get_type().get_element_type();
	}
};

template <typename T>
class clr_list : public clr_list_base
{
public:
	using value_type = T;

	static_assert(is_clr_valuetype<value_type>::value, "Not a value type");

	explicit clr_list(const clr_object& obj)
		: clr_list_base(obj)
	{
	}

	explicit clr_list(managed_ptr obj)
		: clr_list_base(std::move(obj))
	{
	}

	auto create_list(const clr_domain& domain, const clr_type& element_type) -> clr_object
	{
		auto type = element_type.valid() ? element_type : detail::builtin_element_type<T>();
		if(!type.valid())
		{
			return {};
		}

		auto list_type = type.get_list_type();
		if(!list_type.valid())
		{
			return {};
		}

		return list_type.new_instance(domain);
	}

	template <typename VectorLike = std::vector<T>>
	clr_list(const VectorLike& vec)
		: clr_list_base(clr_object{})
	{
		auto obj = create_list(clr_domain::get_current_domain(), {});
		object_ = obj.get_managed_ptr();
		type_ = obj.get_type();
		for(auto& item : vec)
		{
			add(item);
		}
	}

	template <typename VectorLike = std::vector<T>>
	clr_list(const VectorLike& vec, const clr_type& element_type)
		: clr_list_base(clr_object{})
	{
		auto obj = create_list(clr_domain::get_current_domain(), element_type);
		object_ = obj.get_managed_ptr();
		type_ = obj.get_type();
		for(auto& item : vec)
		{
			add(item);
		}
	}

	void add(const T& value)
	{
		auto invoker = make_method_invoker<void(const T&)>(get_type(), "Add");
		invoker(*this, value);
	}

	auto get(std::size_t index) const -> T
	{
		int idx = static_cast<int>(index);
		auto invoker = make_property_invoker<T>(get_type(), "Item");
		return invoker.get_value_with_args(*this, idx);
	}

	void set(std::size_t index, const T& value)
	{
		int idx = static_cast<int>(index);
		auto invoker = make_property_invoker<T>(get_type(), "Item");
		invoker.set_value_with_args(*this, idx, value);
	}

	auto to_list() const -> std::list<T>
	{
		std::list<T> result;
		std::size_t n = size();
		for(std::size_t i = 0; i < n; i++)
		{
			result.push_back(get(i));
		}
		return result;
	}

	template <typename VectorLike = std::vector<T>>
	auto to_vector() const -> VectorLike
	{
		VectorLike vec(size());
		for(size_t i = 0; i < vec.size(); ++i)
		{
			vec[i] = get(i);
		}
		return vec;
	}
};

template <>
class clr_list<clr_object> : public clr_list_base
{
public:
	using value_type = clr_object;

	explicit clr_list(const clr_object& obj)
		: clr_list_base(obj)
	{
	}

	explicit clr_list(managed_ptr obj)
		: clr_list_base(std::move(obj))
	{
	}

	template <typename VectorLike = std::vector<clr_object>>
	clr_list(const VectorLike& vec, const clr_type& element_type)
		: clr_list_base(clr_object{})
	{
		auto list_type = element_type.get_list_type();
		if(list_type.valid())
		{
			auto obj = list_type.new_instance();
			object_ = obj.get_managed_ptr();
			type_ = obj.get_type();
		}
		for(auto& item : vec)
		{
			add(item);
		}
	}

	template <typename VectorLike = std::vector<clr_object>>
	void set(const VectorLike& vec, const clr_type& element_type, bool create_missing_elements = false)
	{
		if(!valid())
		{
			return;
		}

		clear();
		for(auto& item : vec)
		{
			auto item_to_add = item;
			if(create_missing_elements && !item_to_add.valid())
			{
				item_to_add = element_type.new_instance();
			}
			add(item_to_add);
		}
	}

	void add(const clr_object& value)
	{
		auto invoker = make_method_invoker<void(clr_object)>(get_type().get_method("Add", 1), false);
		invoker(*this, value);
	}

	auto get(std::size_t index) const -> clr_object
	{
		auto invoker =
			make_method_invoker<clr_object(int32_t)>(get_type().get_method("get_Item", 1), false);
		return invoker(const_cast<clr_list&>(*this), static_cast<int32_t>(index));
	}

	void set(std::size_t index, const clr_object& value)
	{
		auto invoker =
			make_method_invoker<void(int32_t, clr_object)>(get_type().get_method("set_Item", 2), false);
		invoker(*this, static_cast<int32_t>(index), value);
	}

	template <typename VectorLike = std::vector<clr_object>>
	auto to_vector() const -> VectorLike
	{
		auto element_type = get_element_type();
		VectorLike vec(size());
		for(size_t i = 0; i < vec.size(); ++i)
		{
			vec[i] = get(i);
			if(!vec[i].get_type().valid())
			{
				vec[i] = clr_object(managed_ptr{}, element_type);
			}
		}
		return vec;
	}

	template <typename VectorLike = std::vector<clr_object>>
	auto to_vector_wrapper() const -> vector_like_wrapper<VectorLike>
	{
		return {get_element_type(), to_vector<VectorLike>()};
	}
};

// clr_converter specializations for clr_list / std::list
template <typename T>
struct clr_converter<clr_list<T>>
{
	using native_type = clr_list<T>;
	using managed_type = managed_ptr;

	static auto to_mono(const native_type& obj) -> managed_type
	{
		return obj.get_managed_ptr();
	}

	static auto from_mono(const managed_type& obj) -> native_type
	{
		return native_type(managed_ptr::share(obj.get()));
	}
};

template <typename T>
struct clr_converter<std::list<T>>
{
	using native_type = std::list<T>;
	using managed_type = managed_ptr;

	static auto to_mono(const native_type& obj) -> managed_type
	{
		std::vector<T> vec(obj.begin(), obj.end());
		return clr_list<T>(vec).get_managed_ptr();
	}

	static auto from_mono(const managed_type& obj) -> native_type
	{
		if(!obj)
		{
			return {};
		}
		return clr_list<T>(managed_ptr::share(obj.get())).to_list();
	}
};

} // namespace clr
