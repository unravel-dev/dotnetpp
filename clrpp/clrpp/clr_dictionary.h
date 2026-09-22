#pragma once

#include "clr_array.h"
#include "clr_config.h"
#include "clr_exception.h"
#include "clr_method_invoker.h"
#include "clr_object.h"
#include "clr_type_conversion.h"

#include <memory>
#include <utility>
#include <vector>

namespace clr
{

namespace detail
{

struct dictionary_invokers
{
	clr_method_invoker<int()> count;
	clr_method_invoker<void()> clear;
	clr_method_invoker<bool(clr_object)> contains_key;
	clr_method_invoker<bool(clr_object)> remove;
	clr_method_invoker<clr_object(clr_object)> get_item;
	clr_method_invoker<void(clr_object, clr_object)> set_item;
	clr_method_invoker<clr_object()> get_keys;
	clr_method_invoker<clr_object()> get_values;

	explicit dictionary_invokers(const clr_type& type)
		: count(make_method_invoker<int()>(type.get_method("get_Count", 0), false))
		, clear(make_method_invoker<void()>(type.get_method("Clear", 0), false))
		, contains_key(make_method_invoker<bool(clr_object)>(type.get_method("ContainsKey", 1), false))
		, remove(make_method_invoker<bool(clr_object)>(type.get_method("Remove", 1), false))
		, get_item(make_method_invoker<clr_object(clr_object)>(type.get_method("get_Item", 1), false))
		, set_item(make_method_invoker<void(clr_object, clr_object)>(type.get_method("set_Item", 2), false))
		, get_keys(make_method_invoker<clr_object()>(type.get_method("get_Keys", 0), false))
		, get_values(make_method_invoker<clr_object()>(type.get_method("get_Values", 0), false))
	{
	}
};

} // namespace detail

/*
 * Wraps a System.Collections.Generic.Dictionary<TKey, TValue>. Keys and values
 * cross as objects (value types boxed); get_key_type / get_value_type tell
 * their types, also for an entry whose value is null.
 */
class clr_dictionary : public clr_object
{
public:
	using entry = std::pair<clr_object, clr_object>;

	explicit clr_dictionary(const clr_object& obj)
		: clr_object(obj)
	{
	}

	explicit clr_dictionary(managed_ptr obj)
		: clr_object(std::move(obj))
	{
	}

	/// A new, empty instance of a closed Dictionary<TKey, TValue> type.
	static auto create(const clr_type& dictionary_type) -> clr_dictionary
	{
		return clr_dictionary(dictionary_type.new_instance());
	}

	auto size() const -> std::size_t
	{
		return static_cast<std::size_t>(invokers().count(*this));
	}

	void clear()
	{
		invokers().clear(*this);
	}

	auto contains_key(const clr_object& key) const -> bool
	{
		return invokers().contains_key(*this, key);
	}

	/// The value under the key. Throws when there is none.
	auto get(const clr_object& key) const -> clr_object
	{
		return invokers().get_item(*this, key);
	}

	/// Adds the entry, or replaces the value of the key when it has one.
	void set(const clr_object& key, const clr_object& value)
	{
		invokers().set_item(*this, key, value);
	}

	/// Returns false when the key has no entry.
	auto remove(const clr_object& key) -> bool
	{
		return invokers().remove(*this, key);
	}

	auto get_key_type() const -> clr_type
	{
		return get_type_argument(key_argument_index);
	}

	auto get_value_type() const -> clr_type
	{
		return get_type_argument(value_argument_index);
	}

	/// The entries, in the dictionary's enumeration order. A null value keeps the value type.
	auto to_vector() const -> std::vector<entry>
	{
		const auto count = size();
		std::vector<entry> entries(count);
		if(count == 0)
		{
			return entries;
		}

		// Keys and Values enumerate in the same order, so their copies pair up by index.
		auto keys = copy_collection(invokers().get_keys(*this), get_key_type(), count);
		auto values = copy_collection(invokers().get_values(*this), get_value_type(), count);
		for(std::size_t i = 0; i < count; ++i)
		{
			entries[i] = {std::move(keys[i]), std::move(values[i])};
		}
		return entries;
	}

	/// Replaces the entries. A later entry wins over an earlier one with the same key,
	/// and an entry without a key is skipped: a dictionary has no null key.
	void assign(const std::vector<entry>& entries, bool create_missing_values = false)
	{
		clear();
		const auto value_type = get_value_type();
		for(const auto& item : entries)
		{
			if(!item.first.valid())
			{
				continue;
			}
			const bool is_missing = create_missing_values && !item.second.valid();
			set(item.first, is_missing ? value_type.new_instance() : item.second);
		}
	}

private:
	static constexpr std::size_t key_argument_index = 0;
	static constexpr std::size_t value_argument_index = 1;

	auto invokers() const -> detail::dictionary_invokers&
	{
		if(!invokers_)
		{
			invokers_ = std::make_shared<detail::dictionary_invokers>(get_type());
		}
		return *invokers_;
	}

	auto get_type_argument(std::size_t index) const -> clr_type
	{
		const auto arguments = get_type().get_generic_arguments();
		return index < arguments.size() ? arguments[index] : clr_type();
	}

	/// Copies a KeyCollection or ValueCollection with one CopyTo into a managed array.
	static auto copy_collection(const clr_object& collection, const clr_type& element_type, std::size_t count)
		-> std::vector<clr_object>
	{
		clr_array<clr_object> scratch(managed_ptr::adopt(
			bridge().array_create(element_type.get_internal_ptr(), static_cast<int64_t>(count))));
		auto copy_to = make_method_invoker<void(clr_array<clr_object>&, int32_t)>(
			collection.get_type().get_method("CopyTo", 2), false);
		copy_to(collection, scratch, 0);

		std::vector<clr_object> elements(count);
		for(std::size_t i = 0; i < count; ++i)
		{
			elements[i] = scratch.get(i);
			if(!elements[i].get_type().valid())
			{
				elements[i] = clr_object(managed_ptr{}, element_type);
			}
		}
		return elements;
	}

	mutable std::shared_ptr<detail::dictionary_invokers> invokers_;
};

template <>
struct clr_converter<clr_dictionary>
{
	using native_type = clr_dictionary;
	using managed_type = managed_ptr;

	static auto to_managed(const native_type& obj) -> managed_type
	{
		return obj.get_managed_ptr();
	}

	static auto from_managed(const managed_type& obj) -> native_type
	{
		return native_type(managed_ptr::share(obj.get()));
	}
};

} // namespace clr
