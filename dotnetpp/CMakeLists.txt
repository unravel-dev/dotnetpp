set(target_name dotnetpp)

# Compile-time backend selection for the unified dotnet:: API.
set(DOTNETPP_BACKEND "coreclr" CACHE STRING "dotnetpp runtime backend (mono|coreclr)")
set_property(CACHE DOTNETPP_BACKEND PROPERTY STRINGS mono coreclr)

add_library(${target_name} INTERFACE)

target_include_directories(${target_name} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

if(DOTNETPP_BACKEND STREQUAL "mono")
    message(STATUS "dotnetpp: using mono backend (monopp)")
    target_compile_definitions(${target_name} INTERFACE DOTNETPP_BACKEND_MONO=1)
    target_link_libraries(${target_name} INTERFACE monopp)
elseif(DOTNETPP_BACKEND STREQUAL "coreclr")
    message(STATUS "dotnetpp: using coreclr backend (clrpp)")
    target_compile_definitions(${target_name} INTERFACE DOTNETPP_BACKEND_CORECLR=1)
    target_link_libraries(${target_name} INTERFACE clrpp)
else()
    message(FATAL_ERROR "dotnetpp: unknown backend '${DOTNETPP_BACKEND}' (expected 'mono' or 'coreclr')")
endif()
