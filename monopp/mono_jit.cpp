#include "mono_jit.h"
#include "mono_assembly.h"
#include "mono_exception.h"
#include "mono_logger.h"

BEGIN_MONO_INCLUDE
#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/mono-config.h>
#include <mono/metadata/mono-debug.h>
#include <mono/metadata/threads.h>
#include <mono/utils/mono-logger.h>
#include <mono_build_config.h>
END_MONO_INCLUDE

#include <iostream>
#include <sstream>
#include <fstream>

namespace mono
{

namespace
{

static MonoAssembly* preload_hook(MonoAssemblyName *aname, char ** /*assemblies_path*/, void * /*user_data*/)
{
    char *n = mono_stringify_assembly_name(aname);
    log_message("Preload request: " + std::string(n), "trace");
    mono_free(n);
    return NULL; // just logging
}

static MonoAssembly* refonly_preload_hook(MonoAssemblyName *aname, char ** /*assemblies_path*/, void * /*user_data*/)
{
    char *n = mono_stringify_assembly_name(aname);
    log_message("Refonly preload request: " + std::string(n), "trace");
    mono_free(n);
    return NULL; // just logging
}

static void on_log_callback(const char* log_domain, const char* log_level, const char* message,
							mono_bool /*fatal*/, void* /*user_data*/)

{

	//	static const char* mono_error_levels[] = {"error", "critical", "warning",
	//											"message", "info",  "debug"};

	std::string category;
	if(log_level == nullptr)
	{
		category = "warning";
	}
	std::string format_msg;
	if(log_domain)
	{
		format_msg += "[";
		format_msg += log_domain;
		format_msg += "] ";
	}
	format_msg += message;
	log_message(format_msg, category);
}

MonoDomain* jit_domain = nullptr;
compiler_paths* comp_paths = nullptr;

void mono_set_env_var(const char* var_name, const char* value)
{
#if _WIN32
	_putenv_s(var_name, value);
#else
	setenv(var_name, value, 1); // 1 means overwrite if it already exists
#endif
}
} // namespace

auto mono_assembly_dir() -> std::string
{
	return comp_paths->assembly_dir.empty() ? INTERNAL_MONO_ASSEMBLY_DIR : comp_paths->assembly_dir;
}

auto mono_config_dir() -> std::string
{
	return comp_paths->config_dir.empty() ? INTERNAL_MONO_CONFIG_DIR : comp_paths->config_dir;
}

auto mono_msc_executable() -> std::string
{
	return comp_paths->msc_executable.empty() ? INTERNAL_MONO_MCS_EXECUTABLE : comp_paths->msc_executable;
}

auto get_common_library_names() -> const std::vector<std::string>&
{
#ifdef _WIN32
	static const std::vector<std::string> names{"mono-2.0", "monosgen-2.0", "mono-2.0-sgen"};
#else
	static const std::vector<std::string> names{"libmono-2.0", "libmonosgen-2.0", "libmono-2.0-sgen"};
#endif
	return names;
}

auto get_common_library_names_for_deploy() -> const std::vector<std::string>&
{
	static const std::vector<std::string> names	= {
	
#ifndef _WIN32
	"libmono-2.0.so",
	"libmono-2.0.so.1",

	"libmonosgen-2.0.so",
	"libmonosgen-2.0.so.1",

	"libmono-native.so",
	"libmono-native.so.0",

	"libMonoPosixHelper.so",
	"libMonoSupportW.so",

	"libmono-llvm.so",
	"libmono-llvm.so.0",

	"libmono-btls-shared.so",

	"libmono-profiler-aot.so",
	"libmono-profiler-aot.so.0",

	"libmono-profiler-coverage.so",
	"libmono-profiler-coverage.so.0",

	"libmono-profiler-log.so",
	"libmono-profiler-log.so.0"
#endif
	};
	return names;
}
auto get_common_library_paths() -> const std::vector<std::string>&
{
	static const std::vector<std::string> paths{"C:/Program Files/Mono/lib", "/usr/lib64", "/usr/lib",
												"/usr/local/lib64", "/usr/local/lib"};
	return paths;
}

auto get_common_config_paths() -> const std::vector<std::string>&
{
	static const std::vector<std::string> paths{"C:/Program Files/Mono/etc", "/etc", "/etc", "/usr/local/etc",
												"/usr/local/etc"};
	return paths;
}

auto get_common_executable_names() -> const std::vector<std::string>&
{
#ifdef _WIN32
	static const std::vector<std::string> names{"mcs.bat"};
#else
	static const std::vector<std::string> names{"mcs"};

#endif

	return names;
}
auto get_common_executable_paths() -> const std::vector<std::string>&
{
	static const std::vector<std::string> paths{"C:/Program Files/Mono/bin", "/usr/bin", "/usr/bin",
												"/usr/local/bin", "/usr/local/bin"};
	return paths;
}

auto init(const compiler_paths& paths, const debugging_config& debugging,
		  const interpreter_config& interpreter) -> bool
{
	comp_paths = new compiler_paths(paths);

	auto assembly_dir = mono_assembly_dir();
	auto config_dir = mono_config_dir();
	auto asmpath = assembly_dir;
	
#ifdef _WIN32
    asmpath += "\\mono\\4.5";
    mono_set_env_var("MONO_GAC_PREFIX", "");                          // disable system GAC
    mono_set_env_var("MONO_PATH", asmpath.c_str());
#else
    asmpath += "/mono/4.5";
    mono_set_env_var("MONO_GAC_PREFIX", "");
    mono_set_env_var("MONO_PATH", asmpath.c_str());
#endif

	mono_set_dirs(assembly_dir.c_str(), config_dir.c_str());

	mono_set_assemblies_path(asmpath.c_str());           // *** critical ***


	mono_set_crash_chaining(true);
	mono_set_signal_chaining(true);

	mono_install_assembly_preload_hook(preload_hook, NULL);
	mono_install_assembly_refonly_preload_hook(refonly_preload_hook, NULL);

#ifndef _WIN32
	// Adjust GC threads suspending mode on Linux
	mono_set_env_var("MONO_THREADS_SUSPEND", "preemptive");
#else

#endif

	if(debugging.enable_debugging)
	{

		// Create the debugger agent string dynamically
		std::ostringstream debugger_agent;
		debugger_agent << "--debugger-agent=transport=dt_socket,suspend=n,server=y,address="
					   << debugging.address << ":" << debugging.port
					   << ",embedding=1,loglevel=" << debugging.loglevel;

		// keep string alive as we are passing pointers bellow
		std::string result = debugger_agent.str();

		// clang-format off
		const char* options[] =
		{
			"--soft-breakpoints",
			result.c_str(),
			"--debug-domain-unload",

			// GC options:
			// check-remset-consistency: Makes sure that write barriers are properly issued in native code,
			// and therefore
			//    all old->new generation references are properly present in the remset. This is easy to mess
			//    up in native code by performing a simple memory copy without a barrier, so it's important to
			//    keep the option on.
			// verify-before-collections: Unusure what exactly it does, but it sounds like it could help track
			// down
			//    things like accessing released/moved objects, or attempting to release handles for an
			//    unloaded domain.
			// xdomain-checks: Makes sure that no references are left when a domain is unloaded.
			"--gc-debug=check-remset-consistency,verify-before-collections,xdomain-checks"
		};

		// clang-format on
		mono_jit_parse_options(sizeof(options) / sizeof(char*), const_cast<char**>(options));
		mono_debug_init(MONO_DEBUG_FORMAT_MONO);
	}

	auto config_file = config_dir + "/mono/config";
	mono_config_parse(config_file.c_str());
	mono_trace_set_level_string("warning");
	mono_trace_set_log_handler(on_log_callback, nullptr);

	set_log_handler("default", [](const std::string& msg) { std::cout << msg << std::endl; });

	if(interpreter.interp_mode == interpreter_config::mode::forced)
	{
		const char* options[] = {"--interpreter"};
		mono_jit_parse_options(1, const_cast<char**>(options));
		log_message("monopp: mono interpreter forced (--interpreter)", "info");
	}

	jit_domain = mono_jit_init_version("mono_jit", "v4.0.30319");

	
	log_message("mscorlib was loaded from: " + get_core_assembly_path(), "trace");

	mono_thread_set_main(mono_thread_current());

	return jit_domain != nullptr;
}

auto get_core_assembly_path() -> std::string
{
	MonoImage* corlib = mono_get_corlib();
	if(!corlib)
	{
		return {};
	}

	std::string corlib_path = mono_image_get_filename(corlib);
	return corlib_path;
}

void shutdown()
{
	if(jit_domain)
	{
		mono_jit_cleanup(jit_domain);
	}
	jit_domain = nullptr;

	if(comp_paths)
	{
		delete comp_paths;
	}
	comp_paths = nullptr;
}

auto quote(const std::string& word) -> std::string
{
	std::string command;
	command.append("\"");
	command.append(word);
	command.append("\"");
	return command;
}

auto create_compile_command(const compiler_params& params) -> std::string
{
	std::string command;
	command.append(quote(mono_msc_executable()));

	for(const auto& path : params.files)
	{
		command += " ";
		command += quote(path);
	}

	if(!params.output_type.empty())
	{
		command += " -target:";
		command += params.output_type;
	}

	if(!params.references.empty())
	{
		command += " -reference:";

		for(const auto& ref : params.references)
		{
			command += ref;
			command += ",";
		}

		command.pop_back();
	}

	if(!params.references_locations.empty())
	{
		command += " -lib:";

		for(const auto& loc : params.references_locations)
		{
			command += loc;
			command += ",";
		}

		command.pop_back();
	}

	if(!params.output_doc_name.empty())
	{
		command += " -doc:";
		command += quote(params.output_doc_name);
		if(params.suppress_doc_warnings)
		{
			command +=
				" -nowarn:1570,1571,1572,1573,1574,1580,1581,1584,1587,1589,1590,1591,1592,"
				"1710,1711,1712,1723,1734,1735";
		}
	}

	if(params.suppress_unassigned_field_warnings)
	{
		command += " -nowarn:0649";
	}

	if(params.debug)
	{
		command += " -debug";
	}
	else
	{
		command += " -optimize";
	}

	if(params.unsafe)
	{
		command += " -unsafe";
	}

	command += " -out:";
	command += quote(params.output_name);

#ifdef _WIN32
	command = quote(command);
#endif
	return command;
}

auto create_compile_command_detailed(const compiler_params& params) -> compile_cmd
{
	compile_cmd cmd;
	cmd.cmd = mono_msc_executable();

	for(const auto& path : params.files)
	{
		// cmd.args.emplace_back(quote(path));
		cmd.args.emplace_back(path);
	}

	if(!params.output_type.empty())
	{
		std::string arg;
		arg += "-target:";
		arg += params.output_type;

		cmd.args.emplace_back(arg);
	}

	if(!params.references.empty())
	{
		std::string arg;
		arg += "-reference:";

		for(const auto& ref : params.references)
		{
			// arg += quote(ref);
			arg += ref;

			arg += ",";
		}

		arg.pop_back();
		cmd.args.emplace_back(arg);
	}

	if(!params.references_locations.empty())
	{
		std::string arg;
		arg += "-lib:";

		for(const auto& loc : params.references_locations)
		{
			// arg += quote(loc);
			arg += loc;

			arg += ",";
		}

		arg.pop_back();
		cmd.args.emplace_back(arg);
	}

	if(!params.output_doc_name.empty())
	{
		cmd.args.emplace_back("-doc:" + params.output_doc_name);
		if(params.suppress_doc_warnings)
		{
			cmd.args.emplace_back(
				"-nowarn:1570,1571,1572,1573,1574,1580,1581,1584,1587,1589,1590,1591,1592,"
				"1710,1711,1712,1723,1734,1735");
		}
	}

	if(params.suppress_unassigned_field_warnings)
	{
		cmd.args.emplace_back("-nowarn:0649");
	}

	if(params.debug)
	{
		cmd.args.emplace_back("-debug");
	}
	else
	{
		cmd.args.emplace_back("-optimize");
	}

	if(params.unsafe)
	{
		cmd.args.emplace_back("-unsafe");
	}

	{
		std::string arg;

		arg += "-out:";
		// arg += quote(params.output_name);
		arg += params.output_name;

		cmd.args.emplace_back(arg);
	}

	return cmd;
}

static std::string quote_if_needed(std::string s)
{
    if (s.find_first_of(" \t\"") != std::string::npos)
    {
        std::string out = "\"";
        for (char c : s)
            out += (c == '"') ? "\\\"" : std::string(1, c);
        out += "\"";
        return out;
    }
    return s;
}

auto create_compile_rsp(const compiler_params& p) -> std::string
{
    std::ostringstream rsp;

    // basic options
    if (!p.output_type.empty())
        rsp << "-target:" << p.output_type << "\n";

    if (!p.output_name.empty())
        rsp << "-out:" << quote_if_needed(p.output_name) << "\n";

    if (!p.output_doc_name.empty())
    {
        rsp << "-doc:" << quote_if_needed(p.output_doc_name) << "\n";
        if(p.suppress_doc_warnings)
        {
            rsp << "-nowarn:1570,1571,1572,1573,1574,1580,1581,1584,1587,1589,1590,1591,1592,"
                   "1710,1711,1712,1723,1734,1735\n";
        }
    }

    if(p.suppress_unassigned_field_warnings)
    {
        rsp << "-nowarn:0649\n";
    }

    rsp << (p.debug ? "-debug\n" : "-optimize\n");
    if (p.unsafe)
        rsp << "-unsafe\n";

    // reference search locations
    if (!p.references_locations.empty())
    {
        rsp << "-lib:";
        for (size_t i = 0; i < p.references_locations.size(); ++i)
        {
            if (i) rsp << ",";
            rsp << quote_if_needed(p.references_locations[i]);
        }
        rsp << "\n";
    }

    // references
    for (const auto& ref : p.references)
        rsp << "-r:" << quote_if_needed(ref) << "\n";

    // source files
    for (const auto& file : p.files)
        rsp << quote_if_needed(file) << "\n";

    return rsp.str();
}

auto create_compile_command_detailed_rsp(const compiler_params& p, const std::string& rsp_file) -> compile_cmd
{
	compile_cmd cmd;
	cmd.cmd = mono_msc_executable();
	{
		auto rsp =create_compile_rsp(p);

		std::ofstream rsp_file_stream(rsp_file);
		rsp_file_stream << rsp;
		rsp_file_stream.close();
	}
	cmd.args.emplace_back("@" + quote_if_needed(rsp_file));
	return cmd;
}

auto compile(const compiler_params& params) -> bool
{
	auto command = create_compile_command(params);
	std::cout << command << std::endl;
	return std::system(command.c_str()) == 0;
}

auto is_debugger_attached() -> bool
{
	return mono_is_debugger_attached();
}

} // namespace mono
