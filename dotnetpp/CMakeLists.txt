set(target_name dotnetpp)

add_library(${target_name} INTERFACE)

target_include_directories(${target_name} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

if(DOTNETPP_BACKEND STREQUAL "mono")
    message(STATUS "dotnetpp: using mono backend")
    target_compile_definitions(${target_name} INTERFACE DOTNETPP_BACKEND_MONO=1)
    target_link_libraries(${target_name} INTERFACE monopp)
elseif(DOTNETPP_BACKEND STREQUAL "coreclr")
    message(STATUS "dotnetpp: using coreclr backend")
    target_compile_definitions(${target_name} INTERFACE DOTNETPP_BACKEND_CORECLR=1)
    target_link_libraries(${target_name} INTERFACE clrpp)
else()
    message(FATAL_ERROR "dotnetpp: unknown backend '${DOTNETPP_BACKEND}' (expected 'mono' or 'coreclr')")
endif()
