#pragma once

#include "clr_config.h"
#include "clr_domain.h"
#include "clr_exception.h"
#include "clr_object.h"
#include "clr_type_conversion.h"
#include "clr_type_traits.h"

#include <cstring>
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

inline void copy_blittable_bytes(clr_handle array, int64_t byte_offset, void* dest, int64_t byte_count)
{
	auto copied = bridge().array_copy_to(array, byte_offset, dest, byte_count);
	if(copied != byte_count)
	{
		throw clr_exception("NATIVE::array bulk read failed (element layout mismatch?)");
	}
}

inline void copy_blittable_bytes_from(clr_handle array, int64_t byte_offset, const void* src, int64_t byte_count)
{
	auto copied = bridge().array_copy_from(array, byte_offset, src, byte_count);
	if(copied != byte_count)
	{
		throw clr_exception("NATIVE::array bulk write failed (element layout mismatch?)");
	}
}

template <typename VectorLike>
struct has_contiguous_storage : std::false_type
{
};

template <typename T, typename Alloc>
struct has_contiguous_storage<std::vector<T, Alloc>> : std::true_type
{
};

template <>
struct has_contiguous_storage<std::vector<bool>> : std::false_type
{
};

template <typename T>
using array_managed_type = typename clr_converter<T>::managed_type;

template <typename T>
struct array_element_is_direct
	: std::integral_constant<bool, std::is_same<T, array_managed_type<T>>::value>
{
};

/// Pin-once + memcpy is only valid when both the native T and the managed
/// element layout are trivially copyable (POD converters like Color↔ColorF).
/// Handle wrappers (managed_ptr) must go through array_get/set_element.
template <typename T>
struct array_element_is_pinnable
	: std::integral_constant<bool, std::is_trivially_copyable<T>::value &&
									   std::is_trivially_copyable<array_managed_type<T>>::value>
{
};

template <typename T>
struct array_element_is_handle
	: std::integral_constant<bool, std::is_same<array_managed_type<T>, managed_ptr>::value>
{
};

/// Bridge fallback kind: 0 = direct blob, 1 = object handle, 2 = converted POD blob.
template <typename T>
using array_bridge_kind =
	typename std::conditional<array_element_is_direct<T>::value, std::integral_constant<int, 0>,
							  typename std::conditional<array_element_is_handle<T>::value,
														std::integral_constant<int, 1>,
														std::integral_constant<int, 2>>::type>::type;

template <typename T, typename VectorLike>
inline auto fill_array_from_vector(clr_array<T>& array, const VectorLike& vec)
	-> typename std::enable_if<has_contiguous_storage<VectorLike>::value &&
							   array_element_is_direct<T>::value>::type
{
	if(!vec.empty())
	{
		copy_blittable_bytes_from(array.get_internal_ptr(), 0, vec.data(),
								  static_cast<int64_t>(vec.size() * sizeof(T)));
	}
}

template <typename T, typename VectorLike>
inline auto fill_array_from_vector(clr_array<T>& array, const VectorLike& vec)
	-> typename std::enable_if<!has_contiguous_storage<VectorLike>::value ||
							   !array_element_is_direct<T>::value>::type
{
	for(size_t i = 0; i < vec.size(); ++i)
	{
		array.set(i, vec[i]);
	}
}

template <typename T, typename VectorLike>
inline auto read_array_to_vector(const clr_array<T>& array, VectorLike& vec)
	-> typename std::enable_if<has_contiguous_storage<VectorLike>::value &&
							   array_element_is_direct<T>::value>::type
{
	const auto count = array.size();
	vec.resize(count);
	if(count > 0)
	{
		copy_blittable_bytes(array.get_internal_ptr(), 0, vec.data(),
							 static_cast<int64_t>(count * sizeof(T)));
	}
}

template <typename T, typename VectorLike>
inline auto read_array_to_vector(const clr_array<T>& array, VectorLike& vec)
	-> typename std::enable_if<!has_contiguous_storage<VectorLike>::value ||
							   !array_element_is_direct<T>::value>::type
{
	const auto count = array.size();
	vec.resize(count);
	for(size_t i = 0; i < count; ++i)
	{
		vec[i] = array.get(i);
	}
}

} // namespace detail

template <typename T>
class clr_array : public clr_array_base
{
public:
	using clr_array_base::clr_array_base;
	using managed_element_type = detail::array_managed_type<T>;

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
		detail::fill_array_from_vector(*this, vec);
	}

	template <typename VectorLike = std::vector<T>>
	clr_array(const VectorLike& vec, const clr_type& element_type)
		: clr_array_base(managed_ptr{})
	{
		object_ = create_array(clr_domain::get_current_domain(), vec.size(), element_type);
		type_ = clr_type(bridge().object_get_type(object_.get()));
		detail::fill_array_from_vector(*this, vec);
	}

	auto size() const -> size_t
	{
		auto raw = clr_array_base::size();
		return uses_raw_byte_storage() ? raw / sizeof(T) : raw;
	}

	auto get(size_t index) const -> T
	{
		return get_impl(index, detail::array_element_is_pinnable<T>{});
	}

	auto get_object(size_t index) const -> clr_object override
	{
		auto type = get_element_type();
		auto value = get(index);
		return clr_box_value(value, type);
	}

	void set(size_t index, const T& value)
	{
		set_impl(index, value, detail::array_element_is_pinnable<T>{});
	}

	template <typename VectorLike = std::vector<T>>
	auto to_vector() const -> VectorLike
	{
		VectorLike vec;
		detail::read_array_to_vector(*this, vec);
		return vec;
	}

private:
	auto get_impl(size_t index, std::true_type /*pinnable*/) const -> T
	{
		if(try_ensure_pinned())
		{
			const auto offset = static_cast<size_t>(index) * static_cast<size_t>(pinned_elem_size_);
			if(offset + static_cast<size_t>(pinned_elem_size_) > static_cast<size_t>(pinned_byte_length_))
			{
				throw clr_exception("NATIVE::array get out of range");
			}

			auto* src = static_cast<const char*>(pinned_data_) + offset;
			return read_element_at(src, detail::array_element_is_direct<T>{});
		}

		return get_via_bridge(index, detail::array_bridge_kind<T>{});
	}

	auto get_impl(size_t index, std::false_type /*pinnable*/) const -> T
	{
		return get_via_bridge(index, detail::array_bridge_kind<T>{});
	}

	void set_impl(size_t index, const T& value, std::true_type /*pinnable*/)
	{
		if(try_ensure_pinned())
		{
			const auto offset = static_cast<size_t>(index) * static_cast<size_t>(pinned_elem_size_);
			if(offset + static_cast<size_t>(pinned_elem_size_) > static_cast<size_t>(pinned_byte_length_))
			{
				throw clr_exception("NATIVE::array set out of range");
			}

			auto* dest = static_cast<char*>(pinned_data_) + offset;
			write_element_at(dest, value, detail::array_element_is_direct<T>{});
			return;
		}

		set_via_bridge(index, value, detail::array_bridge_kind<T>{});
	}

	void set_impl(size_t index, const T& value, std::false_type /*pinnable*/)
	{
		set_via_bridge(index, value, detail::array_bridge_kind<T>{});
	}

	auto try_ensure_pinned() const -> bool
	{
		const auto handle = get_internal_ptr();
		if(!handle)
		{
			return false;
		}

		if(pinned_for_ == handle && pinned_data_ != nullptr && pinned_elem_size_ > 0)
		{
			return true;
		}

		clr_array_pin_info info{};
		if(bridge().array_pin_acquire(handle, &info) == 0 || info.data == nullptr || info.element_size <= 0)
		{
			pinned_for_ = nullptr;
			pinned_data_ = nullptr;
			pinned_elem_size_ = 0;
			pinned_byte_length_ = 0;
			return false;
		}

		pinned_for_ = handle;
		pinned_data_ = info.data;
		pinned_elem_size_ = info.element_size;
		pinned_byte_length_ = info.byte_length;
		return true;
	}

	auto read_element_at(const char* src, std::true_type /*direct*/) const -> T
	{
		if(pinned_elem_size_ != static_cast<int32_t>(sizeof(T)))
		{
			throw clr_exception("NATIVE::array element size mismatch");
		}
		T value{};
		std::memcpy(std::addressof(value), src, sizeof(T));
		return value;
	}

	auto read_element_at(const char* src, std::false_type /*converted*/) const -> T
	{
		if(pinned_elem_size_ != static_cast<int32_t>(sizeof(managed_element_type)))
		{
			throw clr_exception("NATIVE::array managed element size mismatch");
		}
		managed_element_type managed{};
		std::memcpy(std::addressof(managed), src, sizeof(managed_element_type));
		return clr_converter<T>::from_mono(managed);
	}

	void write_element_at(char* dest, const T& value, std::true_type /*direct*/) const
	{
		if(pinned_elem_size_ != static_cast<int32_t>(sizeof(T)))
		{
			throw clr_exception("NATIVE::array element size mismatch");
		}
		std::memcpy(dest, std::addressof(value), sizeof(T));
	}

	void write_element_at(char* dest, const T& value, std::false_type /*converted*/) const
	{
		if(pinned_elem_size_ != static_cast<int32_t>(sizeof(managed_element_type)))
		{
			throw clr_exception("NATIVE::array managed element size mismatch");
		}
		managed_element_type managed = clr_converter<T>::to_mono(value);
		std::memcpy(dest, std::addressof(managed), sizeof(managed_element_type));
	}

	auto get_via_bridge(size_t index, std::integral_constant<int, 0> /*direct*/) const -> T
	{
		T value{};
		detail::copy_blittable_bytes(get_internal_ptr(), static_cast<int64_t>(index * sizeof(T)),
									 std::addressof(value), static_cast<int64_t>(sizeof(T)));
		return value;
	}

	auto get_via_bridge(size_t index, std::integral_constant<int, 1> /*handle*/) const -> T
	{
		clr_variant result{};
		result.kind = clr_variant::kind_object_handle;

		clr_exception_info_raw ex{};
		bridge().array_get_element(get_internal_ptr(), static_cast<int64_t>(index), &result, &ex);
		throw_if_exception(ex);

		if(result.kind != clr_variant::kind_object_handle || !result.data)
		{
			return T{};
		}
		return clr_converter<T>::from_mono(managed_ptr::adopt(result.data));
	}

	auto get_via_bridge(size_t index, std::integral_constant<int, 2> /*converted*/) const -> T
	{
		managed_element_type managed{};
		clr_variant result{};
		result.kind = clr_variant::kind_blob;
		result.size = static_cast<int32_t>(sizeof(managed_element_type));
		result.data = std::addressof(managed);

		clr_exception_info_raw ex{};
		bridge().array_get_element(get_internal_ptr(), static_cast<int64_t>(index), &result, &ex);
		throw_if_exception(ex);

		return clr_converter<T>::from_mono(managed);
	}

	void set_via_bridge(size_t index, const T& value, std::integral_constant<int, 0> /*direct*/)
	{
		detail::copy_blittable_bytes_from(get_internal_ptr(), static_cast<int64_t>(index * sizeof(T)),
										  std::addressof(value), static_cast<int64_t>(sizeof(T)));
	}

	void set_via_bridge(size_t index, const T& value, std::integral_constant<int, 1> /*handle*/)
	{
		auto handle = clr_converter<T>::to_mono(value);
		auto variant = to_clr_variant(handle);

		clr_exception_info_raw ex{};
		bridge().array_set_element(get_internal_ptr(), static_cast<int64_t>(index), &variant, &ex);
		throw_if_exception(ex);
	}

	void set_via_bridge(size_t index, const T& value, std::integral_constant<int, 2> /*converted*/)
	{
		managed_element_type managed = clr_converter<T>::to_mono(value);
		clr_variant variant{};
		variant.kind = clr_variant::kind_blob;
		variant.size = static_cast<int32_t>(sizeof(managed_element_type));
		variant.data = std::addressof(managed);

		clr_exception_info_raw ex{};
		bridge().array_set_element(get_internal_ptr(), static_cast<int64_t>(index), &variant, &ex);
		throw_if_exception(ex);
	}

	/// True when this array is a System.Byte[] packing an unknown blittable T
	/// (see create_array fallback). Recovers the flag when wrapping an existing
	/// handle where use_raw_bytes_ was not preserved.
	auto uses_raw_byte_storage() const -> bool
	{
		if(use_raw_bytes_)
		{
			return true;
		}
		if(sizeof(T) <= 1 || !valid())
		{
			return false;
		}
		const auto element = get_element_type();
		const auto byte_type = detail::byte_element_type();
		return element.valid() && byte_type.valid() &&
			   element.get_internal_ptr() == byte_type.get_internal_ptr();
	}

	bool use_raw_bytes_{};

	// Pin-once cache: lifetime tied to the strong handle (released in FreeHandle).
	mutable clr_handle pinned_for_{nullptr};
	mutable void* pinned_data_{nullptr};
	mutable int32_t pinned_elem_size_{0};
	mutable int64_t pinned_byte_length_{0};
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
