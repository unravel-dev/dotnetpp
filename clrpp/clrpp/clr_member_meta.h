#pragma once

#include "clr_config.h"

#include <memory>
#include <string>

namespace clr
{

struct clr_member_meta_info
{
	std::string name;
	std::string fullname;
	std::string full_declname;
	int32_t flags = 0;
};

template<typename Meta>
auto meta_name(const std::shared_ptr<Meta>& meta) -> std::string
{
	return meta ? meta->name : std::string{};
}

template<typename Meta>
auto meta_fullname(const std::shared_ptr<Meta>& meta) -> std::string
{
	return meta ? meta->fullname : std::string{};
}

template<typename Meta>
auto meta_full_declname(const std::shared_ptr<Meta>& meta) -> std::string
{
	return meta ? meta->full_declname : std::string{};
}

} // namespace clr
