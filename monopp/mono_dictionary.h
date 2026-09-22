#pragma once

#include "mono_array.h"
#include "mono_config.h"
#include "mono_domain.h"
#include "mono_exception.h"
#include "mono_method_invoker.h"
#include "mono_object.h"
#include "mono_type_conversion.h"

#include <memory>
#include <utility>
#include <vector>

namespace mono
{

namespace detail
{

struct dictionary_invokers
{
	mono_method_invoker<int()> count;
	mono_method_invoker<void()> clear;
	mono_method_invoker<bool(mono_object)> contains_key;
	mono_method_invoker<bool(mono_object)> remove;
	mono_method_invoker<mono_object(mono_object)> get_item;
	mono_method_invoker<void(mono_object, mono_object)> set_item;
	mono_method_invoker<mono_object()> get_keys;
	mono_method_invoker<mono_object()> get_values;

	explicit dictionary_invokers(const mono_type& type)
		: count(make_method_invoker<int()>(type.get_method("get_Count", 0), false))
		, clear(make_method_invoker<void()>(type.get_method("Clear", 0), false))
		, contains_key(make_method_invoker<bool(mono_object)>(type.get_method("ContainsKey", 1), false))
		, remove(make_method_invoker<bool(mono_object)>(type.get_method("Remove", 1), false))
		, get_item(make_method_invoker<mono_object(mono_object)>(type.get_method("get_Item", 1), false))
		, set_item(make_method_invoker<void(mono_object, mono_object)>(type.get_method("set_Item", 2), false))
		, get_keys(make_method_invoker<mono_object()>(type.get_method("get_Keys", 0), false))
		, get_values(make_method_invoker<mono_object()>(type.get_method("get_Values", 0), false))
	{
	}
};

} // namespace detail

/*
 * Wraps a System.Collections.Generic.Dictionary<TKey, TValue>. Keys and values
 * cross as objects (value types boxed); get_key_type / get_value_type tell
 * their types, also for an entry whose value is null.
 */
class mono_dictionary : public mono_object
{
public:
	using entry = std::pair<mono_object, mono_object>;

	explicit mono_dictionary(const mono_object& obj)
		: mono_object(obj)
	{
	}

	/// A new, empty instance of a closed Dictionary<TKey, TValue> type.
	static auto create(const mono_type& dictionary_type) -> mono_dictionary
	{
		return mono_dictionary(dictionary_type.new_instance());
	}

	auto size() const -> std::size_t
	{
		return static_cast<std::size_t>(invokers().count(*this));
	}

	void clear()
	{
		invokers().clear(*this);
	}

	auto contains_key(const mono_object& key) const -> bool
	{
		return invokers().contains_key(*this, key);
	}

	/// The value under the key. Throws when there is none.
	auto get(const mono_object& key) const -> mono_object
	{
		return invokers().get_item(*this, key);
	}

	/// Adds the entry, or replaces the value of the key when it has one. A null
	/// value of a value type stores its default, as it does on CoreCLR.
	void set(const mono_object& key, const mono_object& value)
	{
		const auto value_type = get_value_type();
		const bool is_default = !value.valid() && value_type.is_valuetype();
		invokers().set_item(*this, key, is_default ? value_type.new_instance() : value);
	}

	/// Returns false when the key has no entry.
	auto remove(const mono_object& key) -> bool
	{
		return invokers().remove(*this, key);
	}

	auto get_key_type() const -> mono_type
	{
		return get_type_argument(key_argument_index);
	}

	auto get_value_type() const -> mono_type
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

	auto get_type_argument(std::size_t index) const -> mono_type
	{
		const auto arguments = get_type().get_generic_arguments();
		return index < arguments.size() ? arguments[index] : mono_type();
	}

	/// Copies a KeyCollection or ValueCollection with one CopyTo into a managed array.
	static auto copy_collection(const mono_object& collection, const mono_type& element_type, std::size_t count)
		-> std::vector<mono_object>
	{
		mono_array<mono_object> scratch(
			mono_array_new(mono_domain_get(), element_type.get_internal_ptr(), static_cast<uintptr_t>(count)));
		auto copy_to = make_method_invoker<void(mono_array<mono_object>&, int32_t)>(
			collection.get_type().get_method("CopyTo", 2), false);
		copy_to(collection, scratch, 0);

		std::vector<mono_object> elements(count);
		for(std::size_t i = 0; i < count; ++i)
		{
			elements[i] = scratch.get(i);
			if(!elements[i].get_type().valid())
			{
				elements[i] = mono_object(nullptr, element_type);
			}
		}
		return elements;
	}

	mutable std::shared_ptr<detail::dictionary_invokers> invokers_;
};

template <>
struct mono_converter<mono_dictionary>
{
	using native_type = mono_dictionary;
	using managed_type = MonoObject*;

	static auto to_managed(const native_type& obj) -> managed_type
	{
		return obj.get_internal_ptr();
	}

	static auto from_managed(const managed_type& obj) -> native_type
	{
		return native_type(mono_object(obj));
	}
};

} // namespace mono
