#pragma once

#include "clr_config.h"
#include "clr_type_conversion.h"

namespace clr
{

/*
 * Internal calls on CoreCLR.
 *
 * Native functions are registered by name ("Full.Type.Name::Method(args)",
 * the same convention as mono_add_internal_call). Managed code binds them
 * lazily through Clrpp.InternalCalls.Get(name), which resolves against this
 * registry, and invokes them via unmanaged function pointers.
 *
 * ABI convention for the managed caller (per parameter type):
 *   - primitives                : passed by value (bools widened to int32,
 *                                 chars widened to uint16 - the default
 *                                 interop treatment would be 4-byte BOOL /
 *                                 ANSI char)
 *   - blittable structs         : passed by value with the CLR layout. The
 *                                 weaver normalizes bool fields to U1 and
 *                                 char fields to U2 marshalling, so the
 *                                 interop layout the runtime produces is
 *                                 byte-identical to the C++ struct. Passing
 *                                 by value (not by pointer) keeps managed
 *                                 wrapper structs compatible with native
 *                                 scalars of the same size (e.g. a managed
 *                                 Entity{uint} maps onto entt::entity).
 *   - strings                   : utf8 const char*, owned by the caller for
 *                                 the duration of the call
 *   - objects/wrapped types     : GCHandle (IntPtr), allocated by the caller
 *                                 for the duration of the call
 * Return values follow the same rules (structs return by value per the
 * platform ABI). Strings are returned as interop allocations the managed
 * side frees (Marshal.FreeCoTaskMem), handles are transferred to the
 * caller.
 *
 * The clr_internal_call() wrapper adapts a typed C++ function into that
 * shape through clr_converter, mirroring monopp's internal_call macro.
 *
 * Threading note: internal calls reachable from managed finalizers run on
 * the finalizer thread. Do not take locks there that any thread may hold
 * while blocking on the GC (e.g. around domain_unload, which runs
 * GC.WaitForPendingFinalizers) - that is a deadlock. Also never store the
 * `this` GCHandle of a finalizing object beyond the call.
 */

void add_internal_call(const std::string& name, void* func);

/// Registry lookup (used by the managed resolver).
auto find_internal_call(const std::string& name) -> void*;

/*
 * IL weaving of mono-style internal calls, as a compile step.
 *
 * clr::compile automatically rewrites the produced assembly: every
 * [MethodImpl(MethodImplOptions.InternalCall)] extern method gets a real
 * body that binds the registered native function and performs the call via
 * a statically-typed calli (no runtime code generation - the woven output
 * is AOT-compatible). C# written for the mono backend therefore compiles
 * and runs on coreclr without changes.
 *
 * weave_assembly can also be called directly on an existing dll (rewrites
 * dll/pdb in place). Requires Mono.Cecil.dll next to Clrpp.Managed.dll.
 * Returns false on error; an assembly without [InternalCall] externs is a
 * successful no-op.
 */
auto weave_assembly(const std::string& assembly_path) -> bool;

/// Allocate an interop string the managed side will free (CoTaskMem on
/// Windows, malloc elsewhere - matches Marshal.FreeCoTaskMem).
auto alloc_interop_string(const std::string& value) -> const char*;

struct internal_call_registry
{
	internal_call_registry(const std::string& type)
		: full_typename(type)
	{
	}

	inline void add_internal_call(const std::string& name, void* func)
	{
		clr::add_internal_call(full_typename + "::" + name, func);
	}

	std::string full_typename;
};

namespace detail
{

auto duplicate_handle_for_transfer(const managed_ptr& owned) -> clr_handle;

// Maps a native parameter type to its C ABI representation for icalls.
// Scalars and structs both travel by value: the managed weaver normalizes
// bool/char field marshalling (U1/U2) so the runtime's interop copy of a
// struct is byte-identical to the C++ layout, and by-value passing keeps
// managed wrapper structs interchangeable with native scalars of the same
// size (e.g. a managed Entity{uint} maps onto entt::entity).
template <typename T, typename Managed = typename clr_converter<std::decay_t<T>>::managed_type>
struct icall_abi
{
	using abi_type = Managed;

	static auto from_abi(const Managed& value) -> std::decay_t<T>
	{
		return clr_converter<std::decay_t<T>>::from_mono(value);
	}

	static auto to_abi(const std::decay_t<T>& value) -> Managed
	{
		return clr_converter<std::decay_t<T>>::to_mono(value);
	}
};

// Bools travel as int32 (0/1). A C++ `bool` return only defines the low
// byte of the return register (MSVC emits `xor al, al`, leaving the upper
// bits as garbage), while the managed calli site expects a fully defined
// value - so `false` could randomly arrive as `true` in C#. Widening to
// int32 on both sides makes the value unambiguous (the woven managed
// thunk applies the mirrored mapping).
template <>
struct icall_abi<bool, bool>
{
	using abi_type = int32_t;

	static auto from_abi(abi_type value) -> bool
	{
		return value != 0;
	}

	static auto to_abi(bool value) -> abi_type
	{
		return value ? 1 : 0;
	}
};

// Chars travel as int32 too. A standalone `char` in an unmanaged calli
// signature would be interop-marshalled as a 1-byte ANSI char (unlike the
// 2-byte utf16 CLR char), and a small return type only defines the low
// bits of the return register - widening sidesteps both.
template <>
struct icall_abi<char16_t, char16_t>
{
	using abi_type = int32_t;

	static auto from_abi(abi_type value) -> char16_t
	{
		return static_cast<char16_t>(value);
	}

	static auto to_abi(char16_t value) -> abi_type
	{
		return static_cast<int32_t>(value);
	}
};

// Pointer parameters (out/ref structs): travel as a pointer to the managed
// representation. The converter's pointer pass-through reinterprets it back
// to the native type (layouts validated by the POD converter registration),
// mirroring monopp's wrapper which strips the pointer for converter lookup.
template <typename T, typename Managed>
struct icall_abi<T*, Managed>
{
	using converter = clr_converter<std::remove_const_t<T>>;
	using abi_type = typename converter::managed_type*;

	static auto from_abi(abi_type value) -> T*
	{
		return converter::from_mono(value);
	}
};

// Converter-managed types travel as raw GCHandles across the boundary.
template <typename T>
struct icall_abi<T, managed_ptr>
{
	using abi_type = clr_handle;

	static auto from_abi(abi_type value) -> std::decay_t<T>
	{
		return clr_converter<std::decay_t<T>>::from_mono(managed_ptr::share(value));
	}

	static auto to_abi(const std::decay_t<T>& value) -> abi_type
	{
		// Transfer ownership to the caller via a duplicated handle.
		auto owned = clr_converter<std::decay_t<T>>::to_mono(value);
		return duplicate_handle_for_transfer(owned);
	}
};

// Strings travel as utf8.
template <>
struct icall_abi<std::string, managed_ptr>
{
	using abi_type = const char*;

	static auto from_abi(abi_type value) -> std::string
	{
		return value ? std::string(value) : std::string{};
	}

	static auto to_abi(const std::string& value) -> abi_type
	{
		return alloc_interop_string(value);
	}
};

template <>
struct icall_abi<const std::string&, managed_ptr> : icall_abi<std::string, managed_ptr>
{
};

template <typename Signature, Signature& func>
struct clr_internal_call_wrapper;

// Returns travel by value (scalars widened, structs per the platform ABI).
template <typename R, typename... Args, R (&func)(Args...)>
struct clr_internal_call_wrapper<R(Args...), func>
{
	static auto CLRPP_CALLTYPE wrapper(typename icall_abi<Args>::abi_type... args) ->
		typename icall_abi<R>::abi_type
	{
		return icall_abi<R>::to_abi(func(icall_abi<Args>::from_abi(args)...));
	}
};

template <typename... Args, void (&func)(Args...)>
struct clr_internal_call_wrapper<void(Args...), func>
{
	static void CLRPP_CALLTYPE wrapper(typename icall_abi<Args>::abi_type... args)
	{
		func(icall_abi<Args>::from_abi(args)...);
	}
};

} // namespace detail

/*!
 * Wrap a function for clr::add_internal_call, with automatic type conversion
 * through clr_converter.
 */
#define clr_internal_call(func)                                                                              \
	reinterpret_cast<void*>(&clr::detail::clr_internal_call_wrapper<decltype(func), func>::wrapper)

} // namespace clr
