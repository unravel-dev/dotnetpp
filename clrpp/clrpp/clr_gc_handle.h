#pragma once

#include "clr_config.h"

#include "clr_array.h"
#include "clr_list.h"
#include "clr_object.h"

namespace clr
{

/*
 * Keeps a managed object alive across GC. On CoreCLR every managed_ptr is
 * already a strong GCHandle, so locking simply shares the handle. (True
 * pinning is unnecessary here since the native side never accesses managed
 * memory directly - everything goes through the bridge.)
 */
class clr_scoped_gc_handle
{
public:
	clr_scoped_gc_handle() = default;
	clr_scoped_gc_handle(const clr_scoped_gc_handle&) noexcept = delete;
	auto operator=(const clr_scoped_gc_handle&) noexcept -> clr_scoped_gc_handle& = delete;

	explicit clr_scoped_gc_handle(const clr_object& obj)
	{
		lock(obj);
	}

	~clr_scoped_gc_handle()
	{
		unlock();
	}

	void lock(const clr_object& obj)
	{
		unlock();
		handle_ = obj.get_managed_ptr();
	}

	void unlock()
	{
		handle_ = {};
	}

	auto is_locked() const -> bool
	{
		return static_cast<bool>(handle_);
	}

	auto get_handle() const -> uint32_t
	{
		return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(handle_.get()));
	}

	auto get_domain_version() const -> intptr_t
	{
		// TODO: Implement this
		return 0;
	}

	auto get_object() const -> clr_object
	{
		if(!handle_)
		{
			return {};
		}
		return clr_object(handle_);
	}

	/// Get the object as a specific type.
	template <typename T>
	auto get_object_as() const -> T
	{
		return T(get_object());
	}

private:
	managed_ptr handle_;
};

using clr_object_pinned = clr_scoped_gc_handle;
using clr_object_pinned_ptr = std::shared_ptr<clr_object_pinned>;

inline auto make_object_pinned(const clr_object& obj) -> clr_object_pinned_ptr
{
	return std::make_shared<clr_object_pinned>(obj);
}

template <typename T>
struct clr_array_pinned : clr_object_pinned
{
	using clr_object_pinned::clr_object_pinned;

	auto get_array() const -> clr_array<T>
	{
		return get_object_as<clr_array<T>>();
	}
};

template <typename T>
using clr_array_pinned_ptr = std::shared_ptr<clr_array_pinned<T>>;

template <typename T>
inline auto make_array_pinned(const clr_array<T>& obj) -> clr_array_pinned_ptr<T>
{
	return std::make_shared<clr_array_pinned<T>>(obj);
}

template <typename T>
struct clr_list_pinned : clr_object_pinned
{
	using clr_object_pinned::clr_object_pinned;

	auto get_list() const -> clr_list<T>
	{
		return get_object_as<clr_list<T>>();
	}
};

template <typename T>
using clr_list_pinned_ptr = std::shared_ptr<clr_list_pinned<T>>;

template <typename T>
inline auto make_list_pinned(const clr_list<T>& obj) -> clr_list_pinned_ptr<T>
{
	return std::make_shared<clr_list_pinned<T>>(obj);
}

template <class Fn>
auto with_pinned(const clr_object& obj, Fn&& fn) -> decltype(fn(clr_object()))
{
	clr_scoped_gc_handle pinned(obj);
	return fn(pinned.get_object());
}

inline auto pin_vector_elements(const std::vector<clr_object>& elements) -> std::vector<clr_object_pinned_ptr>
{
	std::vector<clr_object_pinned_ptr> pins;
	pins.reserve(elements.size());
	for(const auto& elem : elements)
	{
		pins.push_back(make_object_pinned(elem));
	}
	return pins;
}

auto gc_get_heap_size() -> int64_t;
auto gc_get_used_size() -> int64_t;
void gc_collect();

} // namespace clr
