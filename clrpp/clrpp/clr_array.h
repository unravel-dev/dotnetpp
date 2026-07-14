#pragma once

#include "clr_config.h"
#include "clr_domain.h"
#include "clr_exception.h"
#include "clr_object.h"
#include "clr_type_conversion.h"
#include "clr_type_traits.h"

#include <type_traits>
#include <vector>

namespace clr
{

template <typename VectorLike>
struct vector_like_wrapper
{
	using value_type = typename VectorLike::value_type;
	clr_type type;
	VectorLike container;
};

class clr_array_base : public clr_object
{
public:
	explicit clr_array_base(managed_ptr arr)
		: clr_object(std::move(arr))
	{
	}

	explicit clr_array_base(const clr_object& obj)
		: clr_object(obj)
	{
	}

	auto size() const -> size_t
	{
		if(!valid())
		{
			return 0;
		}
		return static_cast<size_t>(bridge().array_get_length(get_internal_ptr()));
	}

	auto get_element_type() const -> clr_type
	{
		return get_type().get_element_type();
	}

	virtual auto get_object(size_t index) const -> clr_object = 0;
};

namespace detail
{

/// Corlib type for well known primitive element types (invalid otherwise).
template <typename T>
inline auto builtin_element_type() -> clr_type
{
	bool found = false;
	const auto& names = types::get_name<T>(found);
	if(!found)
	{
		return {};
	}
	return clr_assembly::get_corlib().get_type(names.fullname);
}

inline auto byte_element_type() -> clr_type
{
	return clr_assembly::get_corlib().get_type("System.Byte");
}

} // namespace detail

template <typename T>
class clr_array : public clr_array_base
{
public:
	using clr_array_base::clr_array_base;

	static_assert(is_clr_valuetype<T>::value, "Specialize clr_array for non-value types");

	auto create_array(const clr_domain& domain, size_t count, const clr_type& element_type) -> managed_ptr
	{
		(void)domain;

		auto type = element_type.valid() ? element_type : detail::builtin_element_type<T>();

		if(type.valid())
		{
			return managed_ptr::adopt(
				bridge().array_create(type.get_internal_ptr(), static_cast<int64_t>(count)));
		}

		// Unknown blittable struct: raw byte array.
		use_raw_bytes_ = true;
		return managed_ptr::adopt(bridge().array_create(detail::byte_element_type().get_internal_ptr(),
														static_cast<int64_t>(count * sizeof(T))));
	}

	template <typename VectorLike = std::vector<T>>
	clr_array(const VectorLike& vec)
		: clr_array_base(managed_ptr{})
	{
		object_ = create_array(clr_domain::get_current_domain(), vec.size(), {});
		type_ = clr_type(bridge().object_get_type(object_.get()));
		for(size_t i = 0; i < vec.size(); ++i)
		{
			set(i, vec[i]);
		}
	}

	template <typename VectorLike = std::vector<T>>
	clr_array(const VectorLike& vec, const clr_type& element_type)
		: clr_array_base(managed_ptr{})
	{
		object_ = create_array(clr_domain::get_current_domain(), vec.size(), element_type);
		type_ = clr_type(bridge().object_get_type(object_.get()));
		for(size_t i = 0; i < vec.size(); ++i)
		{
			set(i, vec[i]);
		}
	}

	auto size() const -> size_t
	{
		auto raw = clr_array_base::size();
		return use_raw_bytes_ ? raw / sizeof(T) : raw;
	}

	auto get(size_t index) const -> T
	{
		// Offsets assume the managed element size equals sizeof(T); a short
		// or failed copy (layout mismatch, reference-bearing elements) must
		// not silently yield a zeroed value.
		T value{};
		auto copied = bridge().array_copy_to(get_internal_ptr(), static_cast<int64_t>(index * sizeof(T)),
											 std::addressof(value), static_cast<int64_t>(sizeof(T)));
		if(copied != static_cast<int64_t>(sizeof(T)))
		{
			throw clr_exception("NATIVE::array element read failed (element layout mismatch?)");
		}
		return value;
	}

	auto get_object(size_t index) const -> clr_object override
	{
		auto type = get_element_type();
		auto value = get(index);
		return clr_box_value(value, type);
	}

	void set(size_t index, const T& value)
	{
		auto copied =
			bridge().array_copy_from(get_internal_ptr(), static_cast<int64_t>(index * sizeof(T)),
									 std::addressof(value), static_cast<int64_t>(sizeof(T)));
		if(copied != static_cast<int64_t>(sizeof(T)))
		{
			throw clr_exception("NATIVE::array element write failed (element layout mismatch?)");
		}
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
	bool use_raw_bytes_{};
};

template <>
class clr_array<clr_object> : public clr_array_base
{
public:
	using clr_array_base::clr_array_base;

	auto create_array(const clr_domain& domain, const clr_type& element_type, size_t count) -> managed_ptr
	{
		(void)domain;
		return managed_ptr::adopt(
			bridge().array_create(element_type.get_internal_ptr(), static_cast<int64_t>(count)));
	}

	template <typename VectorLike = std::vector<clr_object>>
	clr_array(const VectorLike& vec)
		: clr_array_base(managed_ptr{})
	{
		if(!vec.empty())
		{
			object_ = create_array(clr_domain::get_current_domain(), vec[0].get_type(), vec.size());
			type_ = clr_type(bridge().object_get_type(object_.get()));
		}
		for(size_t i = 0; i < vec.size(); ++i)
		{
			set(i, vec[i]);
		}
	}

	template <typename VectorLike = std::vector<clr_object>>
	clr_array(const VectorLike& vec, const clr_type& element_type)
		: clr_array_base(managed_ptr{})
	{
		object_ = create_array(clr_domain::get_current_domain(), element_type, vec.size());
		type_ = clr_type(bridge().object_get_type(object_.get()));
		for(size_t i = 0; i < vec.size(); ++i)
		{
			set(i, vec[i]);
		}
	}

	template <typename VectorLike = std::vector<clr_object>>
	void set(const VectorLike& vec, const clr_type& element_type, bool create_missing_elements = false)
	{
		if(!valid())
		{
			return;
		}

		for(size_t i = 0; i < vec.size(); i++)
		{
			auto item = vec[i];
			if(create_missing_elements && !item.valid())
			{
				item = element_type.new_instance();
			}
			set(i, item);
		}
	}

	auto get(size_t index) const -> clr_object
	{
		clr_variant result;
		result.kind = clr_variant::kind_object_handle;

		clr_exception_info_raw ex{};
		bridge().array_get_element(get_internal_ptr(), static_cast<int64_t>(index), &result, &ex);
		throw_if_exception(ex);

		if(result.kind != clr_variant::kind_object_handle || !result.data)
		{
			return clr_object(managed_ptr{}, get_element_type());
		}
		return clr_object(managed_ptr::adopt(result.data));
	}

	auto get_object(size_t index) const -> clr_object override
	{
		return get(index);
	}

	void set(size_t index, const clr_object& value)
	{
		auto handle = value.get_managed_ptr();
		auto variant = to_clr_variant(handle);

		clr_exception_info_raw ex{};
		bridge().array_set_element(get_internal_ptr(), static_cast<int64_t>(index), &variant, &ex);
		throw_if_exception(ex);
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

} // namespace clr

// clr_converter specializations for clr_array / std::vector
namespace clr
{
template <typename T>
struct clr_converter;

template <typename T>
struct clr_converter<clr_array<T>>
{
	using native_type = clr_array<T>;
	using managed_type = managed_ptr;

	static auto to_mono(const native_type& obj) -> managed_type
	{
		return obj.get_managed_ptr();
	}

	static auto from_mono(const managed_type& obj) -> native_type
	{
		if(!obj)
		{
			return native_type(managed_ptr{});
		}
		return native_type(managed_ptr::share(obj.get()));
	}
};

template <typename T>
struct clr_converter<std::vector<T>>
{
	using native_type = std::vector<T>;
	using managed_type = managed_ptr;

	static auto to_mono(const native_type& obj) -> managed_type
	{
		return clr_array<T>(obj).get_managed_ptr();
	}

	static auto from_mono(const managed_type& obj) -> native_type
	{
		if(!obj)
		{
			return {};
		}
		return clr_array<T>(managed_ptr::share(obj.get())).to_vector();
	}
};
} // namespace clr
