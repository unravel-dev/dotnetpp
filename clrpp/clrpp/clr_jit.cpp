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
#include <memory>
#include <sstream>

namespace clr
{

namespace
{
std::unique_ptr<compiler_paths> g_comp_paths;

/*
 * Directory with the framework reference assemblies
 * (packs/Microsoft.NETCore.App.Ref/<ver>/ref/netX.Y). Unlike mcs, csc has no
 * implicit standard library, so compile commands reference all of these.
 */
auto find_reference_assemblies_dir() -> std::string
{
	if(g_comp_paths && !g_comp_paths->reference_assemblies_dir.empty())
	{
		return g_comp_paths->reference_assemblies_dir;
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
	if(g_comp_paths && !g_comp_paths->msc_executable.empty())
	{
		return g_comp_paths->msc_executable;
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

/// Locate the Roslyn compiler shipped with the preferred / newest SDK.
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

	const auto preferred = path_utils::parse_version(get_dotnet_version());
	for(const auto& root : roots)
	{
		auto sdk_dir = root + "/sdk";
		auto version = path_utils::pick_highest_version_subdir(sdk_dir, preferred);
		if(version.empty())
		{
			continue;
		}
		auto csc = sdk_dir + "/" + version + "/Roslyn/bincore/csc.dll";
		if(path_utils::path_exists(csc))
		{
			return csc;
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

enum class csc_arg_style
{
	command_line, // -reference:path
	response_file // -r:path (and quoted where needed)
};

/// Shared csc option list for command-line and .rsp flavors.
auto build_csc_args(const compiler_params& params, csc_arg_style style) -> std::vector<std::string>
{
	const bool rsp = style == csc_arg_style::response_file;
	const char* reference_prefix = rsp ? "-r:" : "-reference:";

	std::vector<std::string> args;
	args.emplace_back("-nologo");
	args.emplace_back("-nostdlib+");

	for(const auto& ref : framework_references())
	{
		args.emplace_back(std::string(reference_prefix) + (rsp ? quote_if_needed(ref) : ref));
	}

	if(!params.output_type.empty())
	{
		args.emplace_back("-target:" + params.output_type);
	}

	if(!params.output_name.empty())
	{
		args.emplace_back("-out:" + (rsp ? quote_if_needed(params.output_name) : params.output_name));
	}

	if(!params.output_doc_name.empty())
	{
		args.emplace_back("-doc:" + (rsp ? quote_if_needed(params.output_doc_name) : params.output_doc_name));
		if(params.suppress_doc_warnings)
		{
			// XML doc warnings: badly formed / param / cref / include / missing /
			// typeparam / paramref (CS1570-CS1592, CS1710-CS1712, CS1723, CS1734-CS1735).
			args.emplace_back(
				"-nowarn:1570,1571,1572,1573,1574,1580,1581,1584,1587,1589,1590,1591,1592,"
				"1710,1711,1712,1723,1734,1735");
		}
	}

	if(params.suppress_unassigned_field_warnings)
	{
		args.emplace_back("-nowarn:0649");
	}

	args.emplace_back(params.debug ? "-debug:portable" : "-optimize");

	if(params.unsafe)
	{
		args.emplace_back("-unsafe");
	}

	for(const auto& define : params.defines)
	{
		args.emplace_back("-define:" + define);
	}

	if(!params.references_locations.empty())
	{
		std::string arg = "-lib:";
		for(size_t i = 0; i < params.references_locations.size(); ++i)
		{
			if(i)
			{
				arg += ",";
			}
			arg += rsp ? quote_if_needed(params.references_locations[i]) : params.references_locations[i];
		}
		args.emplace_back(arg);
	}

	if(!params.references.empty())
	{
		if(rsp)
		{
			for(const auto& ref : params.references)
			{
				args.emplace_back(std::string(reference_prefix) + quote_if_needed(ref));
			}
		}
		else
		{
			std::string arg = "-reference:";
			for(size_t i = 0; i < params.references.size(); ++i)
			{
				if(i)
				{
					arg += ",";
				}
				arg += params.references[i];
			}
			args.emplace_back(arg);
		}
	}

	for(const auto& path : params.files)
	{
		args.emplace_back(rsp ? quote_if_needed(path) : path);
	}

	return args;
}

/// Resolve `dotnet exec csc.dll` (or a bare `csc` fallback).
auto make_csc_command() -> compile_cmd
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
	return cmd;
}

auto join_command(const compile_cmd& detailed) -> std::string
{
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
	// Snap SDK layout differs by package generation: the classic
	// `dotnet-sdk` snap uses `current/` as DOTNET_ROOT, while versioned
	// snaps (`dotnet-sdk-80` / `-90` / `-100`) keep the host under
	// `current/usr/lib/dotnet`. `/snap` is usually a symlink to
	// `/var/lib/snapd/snap`; both mounts are listed for distros that
	// only expose one of them.
	static const std::vector<std::string> paths{
		"C:/Program Files/dotnet",
		"/usr/share/dotnet",
		"/usr/lib/dotnet",
		"/usr/local/share/dotnet",
		"/opt/dotnet",
		"/snap/bin",
		"/snap/dotnet-sdk/current",
		"/snap/dotnet-sdk/current/usr/lib/dotnet",
		"/snap/dotnet-sdk-90/current",
		"/snap/dotnet-sdk-90/current/usr/lib/dotnet",
		"/snap/dotnet-sdk-100/current",
		"/snap/dotnet-sdk-100/current/usr/lib/dotnet",
		"/var/lib/snapd/snap/dotnet-sdk/current",
		"/var/lib/snapd/snap/dotnet-sdk/current/usr/lib/dotnet",
		"/var/lib/snapd/snap/dotnet-sdk-90/current",
		"/var/lib/snapd/snap/dotnet-sdk-90/current/usr/lib/dotnet",
		"/var/lib/snapd/snap/dotnet-sdk-100/current",
		"/var/lib/snapd/snap/dotnet-sdk-100/current/usr/lib/dotnet",
	};
	return paths;
}

auto get_common_config_paths() -> const std::vector<std::string>&
{
	return get_common_library_paths();
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

auto init(const compiler_paths& paths, const debugging_config& debugging, const interpreter_config& interpreter)
	-> bool
{
	g_comp_paths = std::make_unique<compiler_paths>(paths);

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
	g_comp_paths.reset();
}

auto create_compile_command(const compiler_params& params) -> std::string
{
	return join_command(create_compile_command_detailed(params));
}

auto create_compile_command_detailed(const compiler_params& params) -> compile_cmd
{
	auto cmd = make_csc_command();
	for(auto& arg : build_csc_args(params, csc_arg_style::command_line))
	{
		cmd.args.emplace_back(std::move(arg));
	}
	return cmd;
}

auto create_compile_rsp(const compiler_params& p) -> std::string
{
	std::ostringstream rsp;
	for(const auto& arg : build_csc_args(p, csc_arg_style::response_file))
	{
		rsp << arg << "\n";
	}
	return rsp.str();
}

auto create_compile_command_detailed_rsp(const compiler_params& p, const std::string& rsp_file)
	-> compile_cmd
{
	auto cmd = make_csc_command();
	{
		std::ofstream rsp_file_stream(rsp_file);
		rsp_file_stream << create_compile_rsp(p);
	}
	cmd.args.emplace_back("@" + quote_if_needed(rsp_file));
	return cmd;
}

auto compile(const compiler_params& params) -> bool
{
	// The full reference set exceeds command line limits; go through an rsp.
	auto rsp_file = params.output_name + ".rsp";
	auto detailed = create_compile_command_detailed_rsp(params, rsp_file);

	std::string command = join_command(detailed);
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
