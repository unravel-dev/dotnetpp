#pragma once

#include "dotnet_config.h"
#include "dotnet_array.h"
#include "dotnet_list.h"
#include "dotnet_object.h"

#if DOTNETPP_BACKEND_MONO
#include <monopp/mono_gc_handle.h>

namespace dotnet
{

using scoped_gc_handle = mono::mono_scoped_gc_handle;

using object_pinned = mono::mono_object_pinned;
using object_pinned_ptr = mono::mono_object_pinned_ptr;
using mono::make_object_pinned;

template <typename T>
using array_pinned = mono::mono_array_pinned<T>;
template <typename T>
using array_pinned_ptr = mono::mono_array_pinned_ptr<T>;
using mono::make_array_pinned;

template <typename T>
using list_pinned = mono::mono_list_pinned<T>;
template <typename T>
using list_pinned_ptr = mono::mono_list_pinned_ptr<T>;
using mono::make_list_pinned;

using mono::with_pinned;
using mono::pin_vector_elements;

using mono::gc_get_heap_size;
using mono::gc_get_used_size;
using mono::gc_collect;

} // namespace dotnet

#elif DOTNETPP_BACKEND_CORECLR
#include <clrpp/clr_gc_handle.h>

namespace dotnet
{

using scoped_gc_handle = clr::clr_scoped_gc_handle;

using object_pinned = clr::clr_object_pinned;
using object_pinned_ptr = clr::clr_object_pinned_ptr;
using clr::make_object_pinned;

template <typename T>
using array_pinned = clr::clr_array_pinned<T>;
template <typename T>
using array_pinned_ptr = clr::clr_array_pinned_ptr<T>;
using clr::make_array_pinned;

template <typename T>
using list_pinned = clr::clr_list_pinned<T>;
template <typename T>
using list_pinned_ptr = clr::clr_list_pinned_ptr<T>;
using clr::make_list_pinned;

using clr::with_pinned;
using clr::pin_vector_elements;

using clr::gc_get_heap_size;
using clr::gc_get_used_size;
using clr::gc_collect;

} // namespace dotnet
#endif
