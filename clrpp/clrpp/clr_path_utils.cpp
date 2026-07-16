#include "clr_path_utils.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcharacter-conversion"
#endif
#include "utf8/unchecked.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace clr
{
namespace path_utils
{

#ifdef _WIN32
auto to_native_path(const std::string& utf8) -> std::wstring
{
	if(utf8.empty())
	{
		return {};
	}
	std::u16string u16;
	u16.reserve(utf8.size());
	utf8::unchecked::utf8to16(utf8.begin(), utf8.end(), std::back_inserter(u16));
	return std::wstring(u16.begin(), u16.end());
}
#else
auto to_native_path(const std::string& utf8) -> std::string
{
	return utf8;
}
#endif

auto path_join(const std::string& a, const std::string& b) -> std::string
{
	if(a.empty())
	{
		return b;
	}
	const char last = a.back();
	if(last == '/' || last == '\\')
	{
		return a + b;
	}
	return a + "/" + b;
}

auto path_exists(const std::string& path) -> bool
{
#ifdef _WIN32
	return ::GetFileAttributesW(to_native_path(path).c_str()) != INVALID_FILE_ATTRIBUTES;
#else
	struct stat st
	{
	};
	return ::stat(path.c_str(), &st) == 0;
#endif
}

auto current_executable_path() -> std::string
{
#ifdef _WIN32
	char buffer[MAX_PATH]{};
	DWORD len = ::GetModuleFileNameA(nullptr, buffer, sizeof(buffer));
	return len > 0 ? std::string(buffer, len) : std::string{};
#elif defined(__APPLE__)
	return {};
#else
	char buffer[4096]{};
	ssize_t len = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
	return len > 0 ? std::string(buffer, static_cast<size_t>(len)) : std::string{};
#endif
}

auto current_directory() -> std::string
{
#ifdef _WIN32
	char buffer[MAX_PATH]{};
	DWORD len = ::GetCurrentDirectoryA(sizeof(buffer), buffer);
	return len > 0 ? std::string(buffer, len) : std::string(".");
#else
	char buffer[4096]{};
	return ::getcwd(buffer, sizeof(buffer)) ? std::string(buffer) : std::string(".");
#endif
}

auto list_subdirectories(const std::string& dir) -> std::vector<std::string>
{
	std::vector<std::string> result;
#ifdef _WIN32
	WIN32_FIND_DATAA find_data;
	auto handle = ::FindFirstFileA((dir + "\\*").c_str(), &find_data);
	if(handle == INVALID_HANDLE_VALUE)
	{
		return result;
	}
	do
	{
		if((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && find_data.cFileName[0] != '.')
		{
			result.emplace_back(find_data.cFileName);
		}
	} while(::FindNextFileA(handle, &find_data));
	::FindClose(handle);
#else
	if(auto* d = ::opendir(dir.c_str()))
	{
		while(auto* entry = ::readdir(d))
		{
			if(entry->d_name[0] == '.')
			{
				continue;
			}
			struct stat st
			{
			};
			if(::stat(path_join(dir, entry->d_name).c_str(), &st) == 0 && S_ISDIR(st.st_mode))
			{
				result.emplace_back(entry->d_name);
			}
		}
		::closedir(d);
	}
#endif
	return result;
}

auto list_files(const std::string& dir, const std::string& extension) -> std::vector<std::string>
{
	std::vector<std::string> result;
#ifdef _WIN32
	WIN32_FIND_DATAA find_data;
	auto handle = ::FindFirstFileA((dir + "\\*" + extension).c_str(), &find_data);
	if(handle == INVALID_HANDLE_VALUE)
	{
		return result;
	}
	do
	{
		if((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
		{
			result.emplace_back(find_data.cFileName);
		}
	} while(::FindNextFileA(handle, &find_data));
	::FindClose(handle);
#else
	if(auto* d = ::opendir(dir.c_str()))
	{
		while(auto* entry = ::readdir(d))
		{
			std::string name = entry->d_name;
			if(name.size() > extension.size() &&
			   name.compare(name.size() - extension.size(), extension.size(), extension) == 0)
			{
				result.emplace_back(name);
			}
		}
		::closedir(d);
	}
#endif
	return result;
}

auto parse_version(const std::string& text) -> std::vector<int>
{
	std::vector<int> parts;
	std::string current;
	// Skip a non-numeric prefix so tfm-style names ("net9.0") parse as 9.0.
	size_t start = 0;
	while(start < text.size() && !(text[start] >= '0' && text[start] <= '9'))
	{
		++start;
	}
	for(size_t i = start; i < text.size(); ++i)
	{
		const char c = text[i];
		if(c == '.')
		{
			parts.push_back(current.empty() ? 0 : std::atoi(current.c_str()));
			current.clear();
		}
		else if(c >= '0' && c <= '9')
		{
			current += c;
		}
		else
		{
			// Stop at preview/rc suffixes ("9.0.0-rc.1" -> 9.0.0).
			break;
		}
	}
	if(!current.empty())
	{
		parts.push_back(std::atoi(current.c_str()));
	}
	return parts;
}

auto pick_highest_version_subdir(const std::string& base) -> std::string
{
	return pick_highest_version_subdir(base, {});
}

auto pick_highest_version_subdir(const std::string& base, const std::vector<int>& preferred)
	-> std::string
{
	std::string best;
	std::string best_match;
	std::vector<int> best_version;
	std::vector<int> best_match_version;

	auto matches_preferred = [&](const std::vector<int>& version) -> bool
	{
		if(preferred.empty() || version.empty())
		{
			return false;
		}
		if(version[0] != preferred[0])
		{
			return false;
		}
		if(preferred.size() > 1 && version.size() > 1 && version[1] != preferred[1])
		{
			return false;
		}
		return true;
	};

	for(const auto& name : list_subdirectories(base))
	{
		auto version = parse_version(name);
		if(version.empty())
		{
			continue;
		}

		if(best.empty() || std::lexicographical_compare(best_version.begin(), best_version.end(),
														version.begin(), version.end()))
		{
			best = name;
			best_version = version;
		}

		if(matches_preferred(version) &&
		   (best_match.empty() ||
			std::lexicographical_compare(best_match_version.begin(), best_match_version.end(),
										 version.begin(), version.end())))
		{
			best_match = name;
			best_match_version = version;
		}
	}

	return best_match.empty() ? best : best_match;
}

auto pick_highest_version_dir(const std::string& base) -> std::string
{
	auto subdir = pick_highest_version_subdir(base);
	return subdir.empty() ? std::string{} : path_join(base, subdir);
}

auto get_env(const char* name) -> std::string
{
#ifdef _WIN32
	char buffer[4096]{};
	DWORD len = GetEnvironmentVariableA(name, buffer, sizeof(buffer));
	return len > 0 && len < sizeof(buffer) ? std::string(buffer, len) : std::string{};
#else
	const char* value = std::getenv(name);
	return value ? value : std::string{};
#endif
}

void set_env(const char* name, const std::string& value)
{
#ifdef _WIN32
	SetEnvironmentVariableA(name, value.c_str());
	_putenv_s(name, value.c_str());
#else
	setenv(name, value.c_str(), 1 /*overwrite*/);
#endif
}

} // namespace path_utils
} // namespace clr
