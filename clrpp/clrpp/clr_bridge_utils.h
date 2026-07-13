#pragma once

#include "clr_config.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace clr
{

template<typename FillFn>
auto fetch_handles(FillFn fill) -> std::vector<clr_handle>
{
	auto count = fill(nullptr, 0);
	std::vector<clr_handle> handles(static_cast<size_t>(count > 0 ? count : 0));
	if(count > 0)
	{
		fill(handles.data(), count);
	}
	return handles;
}

template<typename T, typename FillFn, typename MapFn>
auto fetch_and_map(FillFn fill, MapFn map) -> std::vector<T>
{
	auto handles = fetch_handles(fill);
	std::vector<T> result;
	result.reserve(handles.size());
	for(auto handle : handles)
	{
		result.emplace_back(map(handle));
	}
	return result;
}

template<typename Meta, typename Key, typename PopulateFn>
auto get_or_create_meta(std::unordered_map<Key, std::shared_ptr<Meta>>& cache, Key key, PopulateFn populate)
	-> std::shared_ptr<Meta>
{
	auto it = cache.find(key);
	if(it != cache.end())
	{
		return it->second;
	}
	auto meta = std::make_shared<Meta>();
	populate(*meta);
	cache[key] = meta;
	return meta;
}

} // namespace clr
