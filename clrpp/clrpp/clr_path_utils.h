#pragma once

#include "clr_config.h"

#include <string>
#include <vector>

namespace clr
{
namespace path_utils
{

auto path_join(const std::string& a, const std::string& b) -> std::string;

auto path_exists(const std::string& path) -> bool;

auto current_executable_path() -> std::string;

auto current_directory() -> std::string;

auto list_subdirectories(const std::string& dir) -> std::vector<std::string>;

auto list_files(const std::string& dir, const std::string& extension) -> std::vector<std::string>;

auto parse_version(const std::string& text) -> std::vector<int>;

auto pick_highest_version_subdir(const std::string& base) -> std::string;

auto pick_highest_version_dir(const std::string& base) -> std::string;

auto get_env(const char* name) -> std::string;

/// Set a process environment variable, overwriting any existing value. On
/// Windows both the Win32 and CRT environment copies are updated so the value
/// is visible to GetEnvironmentVariable (which coreclr's config reader uses)
/// as well as getenv.
void set_env(const char* name, const std::string& value);

} // namespace path_utils
} // namespace clr
