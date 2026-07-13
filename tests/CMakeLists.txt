SET(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)

# A single unified suite runs on both backends.
set(libsrc main.cpp dotnetpp_suite.cpp dotnetpp_suite.h)
set(backend_libs dotnetpp)

set(target_name monopp_test)

add_executable (${target_name} ${libsrc})

target_link_libraries(${target_name} PUBLIC ${backend_libs} suitepp)

set_target_properties(${target_name} PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO

)

target_compile_definitions(${target_name} PUBLIC DATA_DIR="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/")


include(target_warning_support)
set_warning_level(${target_name} ultra)

include(target_code_style_support)
set_code_style(${target_name} lower_case check_headers "${extra_flags}")

enable_testing()
add_test(NAME ${target_name} COMMAND ${target_name})

file(GLOB_RECURSE libsharp *.cs)

add_custom_target(copy_resources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/managed ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/managed)
