#include "clr_jit.h"
#include "clr_bridge.h"
#include "clr_exception.h"
#include "clr_internal_call.h"
#include "clr_logger.h"
#include "clr_path_utils.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

namespace clr
{

namespace
{
namespace ANONYMOUS
{
compiler_paths* comp_paths = nullptr;
} // namespace ANONYMOUS

/*
 * Directory with the framework reference assemblies
 * (packs/Microsoft.NETCore.App.Ref/<ver>/ref/netX.Y). Unlike mcs, csc has no
 * implicit standard library, so compile commands reference all of these.
 */
auto find_reference_assemblies_dir() -> std::string
{
	if(ANONYMOUS::comp_paths && !ANONYMOUS::comp_paths->reference_assemblies_dir.empty())
	{
		return ANONYMOUS::comp_paths->reference_assemblies_dir;
	}

	// Compile against the same TFM the bridge / shipped runtime target
	// (CLRPP_DOTNET_VERSION). Picking the highest installed Ref pack breaks
	// deploy: scripts compiled vs net10 fail to load types on a pruned net9
	// runtime root (GetTypes returns empty via ReflectionTypeLoadException).
	const auto preferred = path_utils::parse_version(get_dotnet_version());

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
		auto version = path_utils::pick_highest_version_subdir(packs, preferred);
		if(version.empty())
		{
			continue;
		}

		auto ref_root = packs + "/" + version + "/ref";
		auto tfm = path_utils::pick_highest_version_subdir(ref_root, preferred);
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
	if(ANONYMOUS::comp_paths && !ANONYMOUS::comp_paths->msc_executable.empty())
	{
		return ANONYMOUS::comp_paths->msc_executable;
	}

	const auto& root = bridge_detail::dotnet_root();
	if(!root.empty())
	{
#ifdef _WIN32
		auto candidate = root + "\\dotnet.exe";
#else
		auto candidate = root + "/dotnet";
#endif
		if(path_utils::path_exists(candidate))
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
		auto versions = path_utils::list_subdirectories(sdk_dir);
		std::sort(versions.rbegin(), versions.rend()); // highest version first
		for(const auto& version : versions)
		{
			auto csc = sdk_dir + "/" + version + "/Roslyn/bincore/csc.dll";
			if(path_utils::path_exists(csc))
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
			log_message("clrpp: framework reference assemblies not found (packs/Microsoft.NETCore.App.Ref); "
						"install a .NET SDK or set compiler_paths::reference_assemblies_dir - "
						"compilation will fail with CS0518 errors",
						"error");
			return result;
		}
		for(const auto& name : path_utils::list_files(dir, ".dll"))
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

namespace
{
/*
 * Translate interpreter_config into the runtime's environment switches.
 * Must run before hostfxr/coreclr are loaded - the runtime samples these
 * variables once during startup. Pre-existing environment values are left
 * alone so external overrides (shell, CI, debugging sessions) keep priority.
 */
void apply_interpreter_config(const interpreter_config& interp)
{
	if(interp.interp_mode != interpreter_config::mode::forced)
	{
		return;
	}

	const char* name = "DOTNET_InterpMode";
	const char* value = "1";
	if(!path_utils::get_env(name).empty())
	{
		log_message(std::string("clrpp: ") + name + " already set in environment; keeping it", "info");
		return;
	}

	path_utils::set_env(name, value);
	log_message(std::string("clrpp: ") + name + "=" + value, "info");
	log_message("clrpp: coreclr interpreter forced; runtimes without the interpreter "
				"ignore this switch and run under the JIT",
				"info");
}
} // namespace

auto init(const compiler_paths& paths, const debugging_config& debugging, const interpreter_config& interpreter)
	-> bool
{
	ANONYMOUS::comp_paths = new compiler_paths(paths);

	set_log_handler("default", [](const std::string& msg) { std::cout << msg << std::endl; });

	if(debugging.enable_debugging)
	{
		// CoreCLR debuggers attach through the runtime directly (vsdbg /
		// netcoredbg); nothing to configure on the embedding side.
		log_message("clrpp: managed debugging is handled by the coreclr debugger services", "info");
	}

	apply_interpreter_config(interpreter);

	if(!bridge_detail::initialize(paths.assembly_dir, paths.config_dir, paths.managed_dir, paths.dotnet_version))
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

auto get_dotnet_version() -> const std::string&
{
	return bridge_detail::dotnet_version();
}

void shutdown()
{
	bridge_detail::terminate();

	delete ANONYMOUS::comp_paths;
	ANONYMOUS::comp_paths = nullptr;
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

	if(result)
	{
		// Part of compilation on this backend: rewrite mono-style
		// [InternalCall] externs with real bodies (see clr_internal_call.h).
		result = weave_assembly(params.output_name);
	}

	return result;
}

auto is_debugger_attached() -> bool
{
	// Answered managed-side via System.Diagnostics.Debugger.IsAttached.
	if(!bridge_alive())
	{
		return false;
	}
	return bridge().is_debugger_attached() != 0;
}

} // namespace clr
