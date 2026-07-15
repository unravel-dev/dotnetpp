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
#include <memory>
#include <vector>

namespace clr
{

namespace detail
{

struct list_base_invokers
{
	clr_method_invoker<int()> count;
	clr_method_invoker<void()> clear;
	clr_method_invoker<void(int32_t)> remove_at;

	explicit list_base_invokers(const clr_type& type)
		: count(make_method_invoker<int()>(type.get_method("get_Count", 0), false))
		, clear(make_method_invoker<void()>(type.get_method("Clear", 0), false))
		, remove_at(make_method_invoker<void(int32_t)>(type.get_method("RemoveAt", 1), false))
	{
	}
};

template <typename T>
struct valuetype_list_invokers
{
	clr_method_invoker<void(clr_array<T>&)> add_range;
	clr_method_invoker<void(const T&)> add;
	clr_method_invoker<T(int32_t)> get_item;
	clr_method_invoker<void(int32_t, const T&)> set_item;

	explicit valuetype_list_invokers(const clr_type& type)
		: add_range(make_method_invoker<void(clr_array<T>&)>(type.get_method("AddRange", 1), false))
		, add(make_method_invoker<void(const T&)>(type, "Add"))
		, get_item(make_method_invoker<T(int32_t)>(type.get_method("get_Item", 1), false))
		, set_item(make_method_invoker<void(int32_t, const T&)>(type.get_method("set_Item", 2), false))
	{
	}
};

struct object_list_invokers
{
	clr_method_invoker<void(clr_array<clr_object>&)> add_range;
	clr_method_invoker<void(clr_object)> add;
	clr_method_invoker<clr_object(int32_t)> get_item;
	clr_method_invoker<void(int32_t, clr_object)> set_item;

	explicit object_list_invokers(const clr_type& type)
		: add_range(make_method_invoker<void(clr_array<clr_object>&)>(type.get_method("AddRange", 1), false))
		, add(make_method_invoker<void(clr_object)>(type.get_method("Add", 1), false))
		, get_item(make_method_invoker<clr_object(int32_t)>(type.get_method("get_Item", 1), false))
		, set_item(make_method_invoker<void(int32_t, clr_object)>(type.get_method("set_Item", 2), false))
	{
	}
};

} // namespace detail

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
		ensure_invokers();
		return static_cast<std::size_t>((*invokers_).count(*this));
	}

	void clear()
	{
		ensure_invokers();
		(*invokers_).clear(*this);
	}

	void remove_at(int32_t index)
	{
		ensure_invokers();
		(*invokers_).remove_at(*this, index);
	}

	auto get_element_type() const -> clr_type
	{
		return get_type().get_element_type();
	}

protected:
	void ensure_invokers() const
	{
		if(!invokers_)
		{
			invokers_ = std::make_shared<detail::list_base_invokers>(get_type());
		}
	}

	mutable std::shared_ptr<detail::list_base_invokers> invokers_;
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
		init_from_vector(vec, {});
	}

	template <typename VectorLike = std::vector<T>>
	clr_list(const VectorLike& vec, const clr_type& element_type)
		: clr_list_base(clr_object{})
	{
		init_from_vector(vec, element_type);
	}

	void add(const T& value)
	{
		ensure_typed_invokers();
		(*typed_invokers_).add(*this, value);
	}

	auto get(std::size_t index) const -> T
	{
		ensure_typed_invokers();
		return (*typed_invokers_).get_item(const_cast<clr_list&>(*this), static_cast<int32_t>(index));
	}

	void set(std::size_t index, const T& value)
	{
		ensure_typed_invokers();
		(*typed_invokers_).set_item(*this, static_cast<int32_t>(index), value);
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

private:
	template <typename VectorLike>
	void init_from_vector(const VectorLike& vec, const clr_type& element_type)
	{
		auto obj = create_list(clr_domain::get_current_domain(), element_type);
		object_ = obj.get_managed_ptr();
		type_ = obj.get_type();
		if(vec.empty())
		{
			return;
		}

		clr_array<T> arr(vec, element_type);
		ensure_typed_invokers();
		(*typed_invokers_).add_range(*this, arr);
	}

	void ensure_typed_invokers() const
	{
		if(!typed_invokers_)
		{
			typed_invokers_ = std::make_shared<detail::valuetype_list_invokers<T>>(get_type());
		}
	}

	mutable std::shared_ptr<detail::valuetype_list_invokers<T>> typed_invokers_;
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

		if(vec.empty())
		{
			return;
		}

		clr_array<clr_object> arr(vec, element_type);
		ensure_typed_invokers();
		(*typed_invokers_).add_range(*this, arr);
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
		ensure_typed_invokers();
		(*typed_invokers_).add(*this, value);
	}

	auto get(std::size_t index) const -> clr_object
	{
		ensure_typed_invokers();
		return (*typed_invokers_).get_item(const_cast<clr_list&>(*this), static_cast<int32_t>(index));
	}

	void set(std::size_t index, const clr_object& value)
	{
		ensure_typed_invokers();
		(*typed_invokers_).set_item(*this, static_cast<int32_t>(index), value);
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

private:
	void ensure_typed_invokers() const
	{
		if(!typed_invokers_)
		{
			typed_invokers_ = std::make_shared<detail::object_list_invokers>(get_type());
		}
	}

	mutable std::shared_ptr<detail::object_list_invokers> typed_invokers_;
};

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
