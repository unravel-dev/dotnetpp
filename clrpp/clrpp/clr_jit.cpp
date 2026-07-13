#include "clr_jit.h"
#include "clr_bridge.h"
#include "clr_exception.h"
#include "clr_logger.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace clr
{

namespace
{

compiler_paths* comp_paths = nullptr;

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
		if((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
		   find_data.cFileName[0] != '.')
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
			if(::stat((dir + "/" + entry->d_name).c_str(), &st) == 0 && S_ISDIR(st.st_mode))
			{
				result.emplace_back(entry->d_name);
			}
		}
		::closedir(d);
	}
#endif
	return result;
}

auto file_exists(const std::string& path) -> bool
{
	std::ifstream f(path);
	return f.good();
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

auto parse_version_digits(const std::string& text) -> std::vector<int>
{
	std::vector<int> parts;
	std::string current;
	for(char c : text)
	{
		if(c >= '0' && c <= '9')
		{
			current += c;
		}
		else if(c == '.')
		{
			parts.push_back(current.empty() ? 0 : std::atoi(current.c_str()));
			current.clear();
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
	std::string best;
	std::vector<int> best_version;
	for(const auto& name : list_subdirectories(base))
	{
		auto version = parse_version_digits(name);
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
	}
	return best;
}

/*
 * Directory with the framework reference assemblies
 * (packs/Microsoft.NETCore.App.Ref/<ver>/ref/netX.Y). Unlike mcs, csc has no
 * implicit standard library, so compile commands reference all of these.
 */
auto find_reference_assemblies_dir() -> std::string
{
	std::vector<std::string> roots;
	if(!bridge_detail::dotnet_root().empty())
	{
		roots.push_back(bridge_detail::dotnet_root());
	}
	for(const auto& path : get_common_library_paths())
	{
		roots.push_back(path);
	}

	for(const auto& root : roots)
	{
		auto packs = root + "/packs/Microsoft.NETCore.App.Ref";
		auto version = pick_highest_version_subdir(packs);
		if(version.empty())
		{
			continue;
		}

		auto ref_root = packs + "/" + version + "/ref";
		auto tfm = pick_highest_version_subdir(ref_root);
		if(tfm.empty())
		{
			continue;
		}

		return ref_root + "/" + tfm;
	}
	return {};
}

auto clr_dotnet_executable() -> std::string
{
	if(comp_paths && !comp_paths->msc_executable.empty())
	{
		return comp_paths->msc_executable;
	}

	const auto& root = bridge_detail::dotnet_root();
	if(!root.empty())
	{
#ifdef _WIN32
		auto candidate = root + "\\dotnet.exe";
#else
		auto candidate = root + "/dotnet";
#endif
		if(file_exists(candidate))
		{
			return candidate;
		}
	}
	return "dotnet";
}

/// Locate the Roslyn compiler shipped with the newest installed SDK.
auto find_csc_dll() -> std::string
{
	std::vector<std::string> roots;
	if(!bridge_detail::dotnet_root().empty())
	{
		roots.push_back(bridge_detail::dotnet_root());
	}
	for(const auto& path : get_common_library_paths())
	{
		roots.push_back(path);
	}

	for(const auto& root : roots)
	{
		auto sdk_dir = root + "/sdk";
		auto versions = list_subdirectories(sdk_dir);
		std::sort(versions.rbegin(), versions.rend()); // highest version first
		for(const auto& version : versions)
		{
			auto csc = sdk_dir + "/" + version + "/Roslyn/bincore/csc.dll";
			if(file_exists(csc))
			{
				return csc;
			}
		}
	}
	return {};
}

auto quote(const std::string& word) -> std::string
{
	return "\"" + word + "\"";
}

auto quote_if_needed(std::string s) -> std::string
{
	if(s.find_first_of(" \t\"") != std::string::npos)
	{
		std::string out = "\"";
		for(char c : s)
		{
			out += (c == '"') ? "\\\"" : std::string(1, c);
		}
		out += "\"";
		return out;
	}
	return s;
}

/// Framework reference assemblies, resolved once.
auto framework_references() -> const std::vector<std::string>&
{
	static const std::vector<std::string> refs = []()
	{
		std::vector<std::string> result;
		auto dir = find_reference_assemblies_dir();
		if(dir.empty())
		{
			return result;
		}
		for(const auto& name : list_files(dir, ".dll"))
		{
			result.push_back(dir + "/" + name);
		}
		return result;
	}();
	return refs;
}

/// csc-style option list shared by all command flavors.
auto build_csc_args(const compiler_params& params) -> std::vector<std::string>
{
	std::vector<std::string> args;
	args.emplace_back("-nologo");
	args.emplace_back("-nostdlib+");

	// csc has no implicit standard library - reference the framework pack.
	for(const auto& ref : framework_references())
	{
		args.emplace_back("-reference:" + ref);
	}

	for(const auto& path : params.files)
	{
		args.emplace_back(path);
	}

	if(!params.output_type.empty())
	{
		args.emplace_back("-target:" + params.output_type);
	}

	if(!params.references.empty())
	{
		std::string arg = "-reference:";
		for(const auto& ref : params.references)
		{
			arg += ref;
			arg += ",";
		}
		arg.pop_back();
		args.emplace_back(arg);
	}

	if(!params.references_locations.empty())
	{
		std::string arg = "-lib:";
		for(const auto& loc : params.references_locations)
		{
			arg += loc;
			arg += ",";
		}
		arg.pop_back();
		args.emplace_back(arg);
	}

	for(const auto& define : params.defines)
	{
		args.emplace_back("-define:" + define);
	}

	if(!params.output_doc_name.empty())
	{
		args.emplace_back("-doc:" + params.output_doc_name);
	}

	if(params.debug)
	{
		args.emplace_back("-debug:portable");
	}
	else
	{
		args.emplace_back("-optimize");
	}

	if(params.unsafe)
	{
		args.emplace_back("-unsafe");
	}

	args.emplace_back("-out:" + params.output_name);

	return args;
}

} // namespace

auto get_common_library_names() -> const std::vector<std::string>&
{
#ifdef _WIN32
	static const std::vector<std::string> names{"hostfxr", "coreclr"};
#else
	static const std::vector<std::string> names{"libhostfxr", "libcoreclr"};
#endif
	return names;
}

auto get_common_library_names_for_deploy() -> const std::vector<std::string>&
{
	// The runtime is resolved from the installed dotnet root; nothing to deploy.
	static const std::vector<std::string> names{};
	return names;
}

auto get_common_library_paths() -> const std::vector<std::string>&
{
	static const std::vector<std::string> paths{"C:/Program Files/dotnet", "/usr/share/dotnet",
												"/usr/lib/dotnet", "/usr/local/share/dotnet",
												"/opt/dotnet"};
	return paths;
}

auto get_common_config_paths() -> const std::vector<std::string>&
{
	static const std::vector<std::string> paths{"C:/Program Files/dotnet", "/usr/share/dotnet",
												"/usr/lib/dotnet", "/usr/local/share/dotnet",
												"/opt/dotnet"};
	return paths;
}

auto get_common_executable_names() -> const std::vector<std::string>&
{
#ifdef _WIN32
	static const std::vector<std::string> names{"dotnet.exe"};
#else
	static const std::vector<std::string> names{"dotnet"};
#endif
	return names;
}

auto get_common_executable_paths() -> const std::vector<std::string>&
{
	return get_common_library_paths();
}

auto init(const compiler_paths& paths, const debugging_config& debugging) -> bool
{
	comp_paths = new compiler_paths(paths);

	set_log_handler("default", [](const std::string& msg) { std::cout << msg << std::endl; });

	if(debugging.enable_debugging)
	{
		// CoreCLR debuggers attach through the runtime directly (vsdbg /
		// netcoredbg); nothing to configure on the embedding side.
		log_message("clrpp: managed debugging is handled by the coreclr debugger services", "info");
	}

	if(!bridge_detail::initialize(paths.assembly_dir, paths.config_dir))
	{
		return false;
	}

	log_message("coreclr runtime loaded from: " + bridge_detail::dotnet_root(), "trace");
	return true;
}

auto get_core_assembly_path() -> std::string
{
	// Reported as the runtime root; coreclr has no standalone mscorlib.
	return bridge_detail::dotnet_root();
}

void shutdown()
{
	bridge_detail::terminate();

	delete comp_paths;
	comp_paths = nullptr;
}

auto create_compile_command(const compiler_params& params) -> std::string
{
	auto detailed = create_compile_command_detailed(params);

	std::string command = quote(detailed.cmd);
	for(const auto& arg : detailed.args)
	{
		command += " ";
		command += quote_if_needed(arg);
	}

#ifdef _WIN32
	command = quote(command);
#endif
	return command;
}

auto create_compile_command_detailed(const compiler_params& params) -> compile_cmd
{
	compile_cmd cmd;
	cmd.cmd = clr_dotnet_executable();

	auto csc = find_csc_dll();
	if(!csc.empty())
	{
		cmd.args.emplace_back("exec");
		cmd.args.emplace_back(csc);
	}
	else
	{
		// Fall back to hoping a csc shim exists on PATH.
		cmd.cmd = "csc";
	}

	for(auto& arg : build_csc_args(params))
	{
		cmd.args.emplace_back(std::move(arg));
	}

	return cmd;
}

auto create_compile_rsp(const compiler_params& p) -> std::string
{
	std::ostringstream rsp;

	rsp << "-nologo\n";
	rsp << "-nostdlib+\n";

	for(const auto& ref : framework_references())
	{
		rsp << "-r:" << quote_if_needed(ref) << "\n";
	}

	if(!p.output_type.empty())
	{
		rsp << "-target:" << p.output_type << "\n";
	}

	if(!p.output_name.empty())
	{
		rsp << "-out:" << quote_if_needed(p.output_name) << "\n";
	}

	if(!p.output_doc_name.empty())
	{
		rsp << "-doc:" << quote_if_needed(p.output_doc_name) << "\n";
	}

	rsp << (p.debug ? "-debug:portable\n" : "-optimize\n");
	if(p.unsafe)
	{
		rsp << "-unsafe\n";
	}

	for(const auto& define : p.defines)
	{
		rsp << "-define:" << define << "\n";
	}

	if(!p.references_locations.empty())
	{
		rsp << "-lib:";
		for(size_t i = 0; i < p.references_locations.size(); ++i)
		{
			if(i)
			{
				rsp << ",";
			}
			rsp << quote_if_needed(p.references_locations[i]);
		}
		rsp << "\n";
	}

	for(const auto& ref : p.references)
	{
		rsp << "-r:" << quote_if_needed(ref) << "\n";
	}

	for(const auto& file : p.files)
	{
		rsp << quote_if_needed(file) << "\n";
	}

	return rsp.str();
}

auto create_compile_command_detailed_rsp(const compiler_params& p, const std::string& rsp_file)
	-> compile_cmd
{
	compile_cmd cmd;
	cmd.cmd = clr_dotnet_executable();

	auto csc = find_csc_dll();
	if(!csc.empty())
	{
		cmd.args.emplace_back("exec");
		cmd.args.emplace_back(csc);
	}
	else
	{
		cmd.cmd = "csc";
	}

	{
		auto rsp = create_compile_rsp(p);

		std::ofstream rsp_file_stream(rsp_file);
		rsp_file_stream << rsp;
	}

	cmd.args.emplace_back("@" + quote_if_needed(rsp_file));
	return cmd;
}

auto compile(const compiler_params& params) -> bool
{
	// The full reference set exceeds command line limits; go through an rsp.
	auto rsp_file = params.output_name + ".rsp";
	auto detailed = create_compile_command_detailed_rsp(params, rsp_file);

	std::string command = quote(detailed.cmd);
	for(const auto& arg : detailed.args)
	{
		command += " ";
		command += quote_if_needed(arg);
	}

#ifdef _WIN32
	command = quote(command);
#endif

	std::cout << command << std::endl;
	auto result = std::system(command.c_str()) == 0;
	std::remove(rsp_file.c_str());
	return result;
}

auto is_debugger_attached() -> bool
{
	// No public embedding query on coreclr; debuggers attach out of band.
	return false;
}

} // namespace clr
