#pragma once
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

namespace clr
{

namespace detail
{

template <typename Tuple, typename F, std::size_t... I>
void for_each_tuple_type_impl(F&& f, std::index_sequence<I...>)
{
	using discard = int[];
	(void)discard{0, (void(f(std::integral_constant<std::size_t, I>{})), 0)...};
}

} // namespace detail

template <typename T>
using is_clr_valuetype = std::is_standard_layout<T>;

template <typename Functor>
struct function_traits : public function_traits<decltype(&Functor::operator())>
{
};

template <typename R, typename... Args>
struct function_traits<R(Args...)>
{
	using return_type = R;
	using function_type = R(Args...);
	constexpr static const std::size_t arity = sizeof...(Args);
	using arg_types = std::tuple<Args...>;
	using arg_types_decayed = std::tuple<std::decay_t<Args>...>;
};

template <typename R, typename... Args>
struct function_traits<R (*)(Args...)> : public function_traits<R(Args...)>
{
};

template <typename C, typename R, typename... Args>
struct function_traits<R (C::*)(Args...)> : public function_traits<R(Args...)>
{
	using owner_type = C&;
};

template <typename C, typename R, typename... Args>
struct function_traits<R (C::*)(Args...) const> : public function_traits<R(Args...)>
{
	using owner_type = const C&;
};

template <typename C, typename R, typename... Args>
struct function_traits<R (C::*)(Args...) volatile> : public function_traits<R(Args...)>
{
	using owner_type = volatile C&;
};

template <typename C, typename R, typename... Args>
struct function_traits<R (C::*)(Args...) const volatile> : public function_traits<R(Args...)>
{
	using owner_type = const volatile C&;
};

template <typename Functor>
struct function_traits<std::function<Functor>> : public function_traits<Functor>
{
};

template <typename T>
struct function_traits<T&> : public function_traits<T>
{
};

template <typename T>
struct function_traits<const T&> : public function_traits<T>
{
};

template <typename T>
struct function_traits<volatile T&> : public function_traits<T>
{
};

template <typename T>
struct function_traits<const volatile T&> : public function_traits<T>
{
};

template <typename T>
struct function_traits<T&&> : public function_traits<T>
{
};

template <typename T>
struct function_traits<const T&&> : public function_traits<T>
{
};

template <typename T>
struct function_traits<volatile T&&> : public function_traits<T>
{
};

template <typename T>
struct function_traits<const volatile T&&> : public function_traits<T>
{
};

/////////////////////////////////////////////////////////////////////

template <typename... Args>
inline void ignore(Args&&...)
{
}

template <typename F, typename Tuple, std::size_t... I>
decltype(auto) apply_impl(F&& f, Tuple&& t, std::index_sequence<I...>)
{
	clr::ignore(t);
	return std::forward<F>(f)(std::get<I>(std::forward<Tuple>(t))...);
}
template <typename F, typename Tuple>
decltype(auto) apply(F&& f, Tuple&& t)
{
	using indices = std::make_index_sequence<std::tuple_size<std::decay_t<Tuple>>::value>;
	return apply_impl(std::forward<F>(f), std::forward<Tuple>(t), indices());
}

template <typename Tuple, typename F, std::size_t... Indices>
void for_each_impl(Tuple&& tuple, F&& f, std::index_sequence<Indices...>)
{
	clr::ignore(tuple, f);
	using swallow = int[];
	(void)swallow{1, (f(std::get<Indices>(std::forward<Tuple>(tuple))), void(), int{})...};
}

template <typename Tuple, typename F>
void for_each(Tuple&& tuple, F&& f)
{
	for_each_impl(std::forward<Tuple>(tuple), std::forward<F>(f),
				  std::make_index_sequence<std::tuple_size<std::remove_reference_t<Tuple>>::value>{});
}

template <class T>
struct tag_t
{
	constexpr tag_t(){};
	using type = T;
};
template <class T>
constexpr tag_t<T> tag{};
template <class Tag>
using type_t = typename Tag::type;

template <class... Ts, class F>
void for_each_type(F&& f)
{
	using discard = int[];
	(void)discard{0, (void(f(tag<Ts>)), 0)...};
}

template <typename Tuple, typename F>
void for_each_tuple_type(F&& f)
{
	constexpr auto size = std::tuple_size<std::decay_t<Tuple>>::value;
	detail::for_each_tuple_type_impl<Tuple>(std::forward<F>(f), std::make_index_sequence<size>{});
}

namespace types
{
using index_t = size_t;

inline auto get_counter() -> index_t&
{
	static index_t value = 0;
	return value;
}

template <typename T>
inline auto id() -> index_t
{
	static index_t sid = get_counter()++;
	return sid;
}

struct type_names_t
{
	std::string name;
	std::string fullname;
};

inline auto get_types() -> const std::map<index_t, type_names_t>&
{
	// clang-format off
	static const std::map<index_t, type_names_t> types =
    {
        {id<std::int8_t>(), {"sbyte", "System.SByte"}},
        {id<std::uint8_t>(), {"byte", "System.Byte"}},
        {id<std::int16_t>(), {"short", "System.Int16"}},
        {id<std::uint16_t>(), {"ushort", "System.UInt16"}},
        {id<std::int32_t>(), {"int", "System.Int32"}},
        {id<std::uint32_t>(), {"uint", "System.UInt32"}},
        {id<std::int64_t>(), {"long", "System.Int64"}},
        {id<std::uint64_t>(), {"ulong", "System.UInt64"}},
        {id<bool>(), {"bool", "System.Boolean"}},
        {id<float>(), {"single", "System.Single"}},
        {id<double>(), {"double", "System.Double"}},
        {id<char16_t>(), {"char", "System.Char"}},
        {id<std::string>(), {"string", "System.String"}},
        {id<void>(), {"void", "System.Void"}}
    };
	// clang-format on

	return types;
}

template <typename T>
inline auto get_name(bool& found) -> const type_names_t&
{
	const auto& types = get_types();
	auto it = types.find(id<std::decay_t<T>>());
	if(it == types.end())
	{
		found |= false;
		static const type_names_t empty{"unknown", "Unknown"};
		return empty;
	}
	found |= true;
	return it->second;
}

template <typename Tuple>
inline auto get_args_signature() -> std::pair<std::string, bool>
{
	// The signature is only usable when EVERY argument type is known
	// (AND-accumulated per argument); a partial signature would bind the
	// wrong overload or fail the lookup outright. for_each_tuple_type hands
	// the lambda an index constant, so the element type must be extracted
	// from the tuple - naming the constant's ::type yields the constant
	// itself and would mark every argument unknown.
	bool all_types_known = true;

	size_t i = 0;
	std::string result;
	clr::for_each_tuple_type<Tuple>(
		[&](auto index)
		{
			using arg_t = std::decay_t<std::tuple_element_t<decltype(index)::value, Tuple>>;

			bool this_type_known = false;
			const auto& name = types::get_name<arg_t>(this_type_known).name;
			all_types_known &= this_type_known;

			if(this_type_known && !name.empty())
			{
				if(i++ > 0)
				{
					result += ',';
				}

				result += name;
			}
		});

	return std::make_pair(result, all_types_known);
}

template <typename T>
auto is_compatible_type(const std::string& expected_name) -> bool
{
	bool found = false;

	const auto& name = types::get_name<T>(found).fullname;

	if(found)
	{
		return name == expected_name;
	}

	return true;
}
} // namespace types
} // namespace clr
