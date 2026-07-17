set(target_name clrpp)

set(CLRPP_MANAGED_DIR "clrpp" CACHE STRING
	"Subdirectory (relative to exe, cwd, or assembly_dir) where Clrpp.Managed runtime files are deployed and probed")

set(CLRPP_DOTNET_VERSION "9.0" CACHE STRING
	"Target .NET version (major.minor). Drives the bridge TargetFramework (net<ver>), the minimum SDK required to build it, and the native default_dotnet_version()")

file(GLOB libsrc clrpp/*.h clrpp/*.cpp)

add_library(${target_name} ${libsrc})

# PUBLIC so consumers (engine/editor deploy code) see the same values.
target_compile_definitions(${target_name}
    PUBLIC
        CLRPP_MANAGED_DIR="${CLRPP_MANAGED_DIR}"
        CLRPP_DOTNET_VERSION="${CLRPP_DOTNET_VERSION}"
)

target_include_directories(${target_name}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(NOT WIN32)
    target_link_libraries(${target_name} PUBLIC dl)
endif()

set_target_properties(${target_name} PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

include(target_warning_support)
set_warning_level(${target_name} ultra)

include(target_code_style_support)
set_code_style(${target_name} lower_case check_headers "${extra_flags}")

# ---------------------------------------------------------------------------
# Managed bridge assembly (Clrpp.Managed.dll)
# ---------------------------------------------------------------------------
option(BUILD_CLRPP_MANAGED "Build the Clrpp.Managed bridge assembly with dotnet" ON)

if(BUILD_CLRPP_MANAGED)
    find_program(DOTNET_EXECUTABLE dotnet)

    if(DOTNET_EXECUTABLE)
        # `dotnet build` uses the latest installed SDK (or the one pinned by a
        # global.json); an SDK older than the targeted TFM fails with
        # NETSDK1045, so catch that at configure time with a clear message.
        execute_process(
            COMMAND ${DOTNET_EXECUTABLE} --version
            OUTPUT_VARIABLE CLRPP_DOTNET_SDK_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )

        if(NOT CLRPP_DOTNET_SDK_VERSION)
            message(FATAL_ERROR
                "clrpp: '${DOTNET_EXECUTABLE} --version' failed - the .NET SDK "
                "appears broken (runtime-only install?). Install a .NET SDK "
                ">= ${CLRPP_DOTNET_VERSION} or set BUILD_CLRPP_MANAGED=OFF "
                "to use a prebuilt Clrpp.Managed.dll.")
        elseif(CLRPP_DOTNET_SDK_VERSION VERSION_LESS CLRPP_DOTNET_VERSION)
            message(FATAL_ERROR
                "clrpp: .NET SDK ${CLRPP_DOTNET_SDK_VERSION} is too old to build "
                "Clrpp.Managed targeting net${CLRPP_DOTNET_VERSION}. Install a "
                "newer SDK, lower CLRPP_DOTNET_VERSION, or set "
                "BUILD_CLRPP_MANAGED=OFF to use a prebuilt Clrpp.Managed.dll.")
        else()
            message(STATUS "clrpp: using .NET SDK ${CLRPP_DOTNET_SDK_VERSION}, targeting net${CLRPP_DOTNET_VERSION}")
        endif()

        set(CLRPP_MANAGED_PROJECT ${CMAKE_CURRENT_SOURCE_DIR}/managed/Clrpp.Managed.csproj)
        set(CLRPP_MANAGED_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/managed/bin/Release)
        set(CLRPP_MANAGED_DLL ${CLRPP_MANAGED_OUTPUT_DIR}/Clrpp.Managed.dll)

        file(GLOB CLRPP_MANAGED_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/managed/*.cs)

        add_custom_command(
            OUTPUT ${CLRPP_MANAGED_DLL}
            COMMAND ${DOTNET_EXECUTABLE} build ${CLRPP_MANAGED_PROJECT} -c Release -o ${CLRPP_MANAGED_OUTPUT_DIR}
                    -p:ClrppDotnetVersion=${CLRPP_DOTNET_VERSION} --nologo -v minimal
            DEPENDS ${CLRPP_MANAGED_SOURCES} ${CLRPP_MANAGED_PROJECT}
            COMMENT "Building Clrpp.Managed bridge assembly (net${CLRPP_DOTNET_VERSION})"
            VERBATIM
        )

        add_custom_target(clrpp_managed DEPENDS ${CLRPP_MANAGED_DLL})
        add_dependencies(${target_name} clrpp_managed)

        if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
            # The whole output dir: bridge dll + runtimeconfig + optional NuGet
            # dependencies (Mono.Cecil, Microsoft.Diagnostics.Runtime, ...).
            # Deployed into a managed-runtime subfolder (CLRPP_MANAGED_DIR) to keep
            # bin clean; the native loader probes <exe_dir>/<CLRPP_MANAGED_DIR> and
            # the bridge resolves its own dependencies from its own directory.
            add_custom_command(TARGET clrpp_managed POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    ${CLRPP_MANAGED_OUTPUT_DIR}
                    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CLRPP_MANAGED_DIR}
                COMMENT "Copying Clrpp.Managed into ${CLRPP_MANAGED_DIR}/ next to the executables"
                VERBATIM
            )
        endif()
    else()
        message(WARNING "clrpp: dotnet executable not found - Clrpp.Managed.dll will not be built")
    endif()
endif()
