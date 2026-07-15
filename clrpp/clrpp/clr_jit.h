#pragma once

#include "clr_config.h"

namespace clr
{

struct compiler_paths
{
	/// Directory containing Clrpp.Managed.dll (+ runtimeconfig). Defaults to
	/// the current working directory.
	std::string assembly_dir;

	/// Optional explicit dotnet root (directory containing host/fxr).
	std::string config_dir;

	/// Optional explicit compiler executable (defaults to "dotnet").
	std::string msc_executable;

	/// Optional explicit directory with framework reference assemblies
	/// (the netX.Y folder under packs/Microsoft.NETCore.App.Ref/<ver>/ref).
	/// When empty it is auto-detected from the dotnet root.
	std::string reference_assemblies_dir;

	/// Subfolder name probed for the managed bridge (Clrpp.Managed.dll)
	/// under assembly_dir, the executable directory and the working
	/// directory. Defaults to the compile-time CLRPP_MANAGED_DIR ("clrpp").
	/// Use assembly_dir to point at an explicit bridge location instead.
	std::string managed_dir = managed_runtime_dir();

	/// Target .NET version as major.minor (e.g. "9.0"). Written into the
	/// fallback runtimeconfig and retrievable via get_dotnet_version() so
	/// tooling (csproj generation, deploys) stays consistent. Note: the
	/// actual runtime loaded is still governed by the bridge runtimeconfig
	/// (rollForward LatestMajor picks the newest installed runtime).
	std::string dotnet_version = default_dotnet_version();
};

struct debugging_config
{
	bool enable_debugging = false;
	std::string address = "127.0.0.1";
	uint32_t port = 55555;
	uint32_t loglevel = 0;
};

/*
 * Experimental CoreCLR interpreter configuration (the "interpreter + R2R"
 * execution model targeted at JIT-restricted platforms such as iOS).
 *
 * The interpreter is selected through process environment variables that the
 * runtime reads during startup, so they must be in place before hostfxr is
 * loaded; clr::init applies them ahead of runtime initialization. Variables
 * already present in the environment are never overridden - an externally
 * set DOTNET_Interpreter / DOTNET_InterpMode always wins, which keeps ad-hoc
 * experiments (and CI overrides) possible without code changes.
 *
 * Note: shipped release runtimes do not yet include the interpreter
 * (FEATURE_INTERPRETER is currently limited to Debug/Checked CoreCLR
 * builds), so on such runtimes these switches are silently ignored and
 * everything runs under the JIT as before. The moment a runtime that ships
 * the interpreter is used (via DOTNET_ROOT or compiler_paths::config_dir),
 * the configuration takes effect with no further changes.
 */
struct interpreter_config
{
	/// Mirrors the runtime's DOTNET_InterpMode values (interpreter_only also
	/// disables ReadyToRun and hardware intrinsics runtime-side).
	enum class mode
	{
		/// Leave the environment untouched; JIT/R2R as usual.
		disabled,
		/// Interpret only the methods matched by `filter` (DOTNET_Interpreter).
		opt_in,
		/// InterpMode=1: interpret everything except R2R-compiled code and
		/// System.Private.CoreLib. This is the iOS shipping model - R2R'd
		/// assemblies run compiled, everything else is interpreted.
		prefer_compiled,
		/// InterpMode=2: interpret everything except intrinsics.
		interpret_all,
		/// InterpMode=3: full interpreter-only mode, no JIT/R2R fallback.
		interpreter_only
	};

	mode interp_mode = mode::disabled;

	/// Methodset for mode::opt_in, e.g. "MyAssembly!*" (whole assembly),
	/// "Namespace.Type::Method" or "*" (everything). Ignored in other modes.
	std::string filter;
};

auto init(const compiler_paths& paths = {}, const debugging_config& debugging = {},
		  const interpreter_config& interpreter = {}) -> bool;
auto get_core_assembly_path() -> std::string;
void shutdown();

/// Target .NET version (major.minor, e.g. "9.0") configured at init.
auto get_dotnet_version() -> const std::string&;

struct compiler_params
{
	// Specifies output assembly name
	std::string output_name;

	std::string output_doc_name;

	// Specifies the input files to compile
	std::vector<std::string> files;

	// Specifies the format of the output assembly
	// Can be one of: exe, winexe, library, module
	std::string output_type = "library";

	// Everything below is optional

	// Imports metadata from the specified assemblies
	std::vector<std::string> references;

	// Specifies the location of referenced assemblies
	std::vector<std::string> references_locations;

	// Defines one or more conditional symbols
	std::vector<std::string> defines;

	// Debug or Optimized?
	bool debug{true};

	// Unsafe mode
	bool unsafe{true};
};

struct compile_cmd
{
	std::string cmd;
	std::vector<std::string> args;
};

auto create_compile_command(const compiler_params& params) -> std::string;
auto create_compile_command_detailed(const compiler_params& params) -> compile_cmd;
auto create_compile_rsp(const compiler_params& p) -> std::string;
auto create_compile_command_detailed_rsp(const compiler_params& p, const std::string& rsp_file) -> compile_cmd;
auto compile(const compiler_params& params) -> bool;

auto get_common_library_names() -> const std::vector<std::string>&;
auto get_common_library_paths() -> const std::vector<std::string>&;
auto get_common_library_names_for_deploy() -> const std::vector<std::string>&;
// same count as library paths
auto get_common_config_paths() -> const std::vector<std::string>&;

auto get_common_executable_names() -> const std::vector<std::string>&;
auto get_common_executable_paths() -> const std::vector<std::string>&;

auto is_debugger_attached() -> bool;

} // namespace clr
