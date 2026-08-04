find_package(Tracy CONFIG REQUIRED)

add_library(tina_trace_tracy STATIC
    TraceTracy.cpp
)
add_library(Tina::TraceTracy ALIAS tina_trace_tracy)

target_compile_features(tina_trace_tracy PUBLIC cxx_std_23)
target_include_directories(tina_trace_tracy
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(tina_trace_tracy
    PRIVATE
        "$<BUILD_INTERFACE:Tina::ProjectOptions>"
        Tracy::TracyClient
)
set_target_properties(tina_trace_tracy PROPERTIES CXX_EXTENSIONS OFF)

if(WIN32)
    get_target_property(tina_tracy_client_type Tracy::TracyClient TYPE)
    if(tina_tracy_client_type STREQUAL "SHARED_LIBRARY")
        if(CMAKE_CONFIGURATION_TYPES)
            set(tina_tracy_runtime_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>")
        else()
            set(tina_tracy_runtime_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
        endif()

        # vcpkg's Tracy Debug package keeps the DLL below debug/bin/Debug,
        # outside the directory scanned by app-local deployment.
        add_custom_target(tina_trace_tracy_stage_runtime
            COMMAND ${CMAKE_COMMAND} -E make_directory "${tina_tracy_runtime_directory}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Tracy::TracyClient>"
                "${tina_tracy_runtime_directory}/$<TARGET_FILE_NAME:Tracy::TracyClient>"
            VERBATIM
        )
        add_dependencies(tina_trace_tracy tina_trace_tracy_stage_runtime)
    endif()
endif()
