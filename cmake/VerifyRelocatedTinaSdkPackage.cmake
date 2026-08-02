if(NOT DEFINED TINA_SDK_RELOCATED_PREFIX OR TINA_SDK_RELOCATED_PREFIX STREQUAL "")
    message(FATAL_ERROR "TINA_SDK_RELOCATED_PREFIX must name the relocated SDK prefix")
endif()
if(NOT DEFINED TINA_SDK_ORIGINAL_PREFIX OR TINA_SDK_ORIGINAL_PREFIX STREQUAL "")
    message(FATAL_ERROR "TINA_SDK_ORIGINAL_PREFIX must name the original SDK prefix")
endif()

cmake_path(ABSOLUTE_PATH TINA_SDK_RELOCATED_PREFIX NORMALIZE OUTPUT_VARIABLE tina_relocated_prefix)
cmake_path(ABSOLUTE_PATH TINA_SDK_ORIGINAL_PREFIX NORMALIZE OUTPUT_VARIABLE tina_original_prefix)

if(NOT IS_DIRECTORY "${tina_relocated_prefix}")
    message(FATAL_ERROR "Relocated Tina SDK prefix does not exist: ${tina_relocated_prefix}")
endif()
if(EXISTS "${tina_original_prefix}")
    message(FATAL_ERROR "Original Tina SDK prefix still exists after relocation: ${tina_original_prefix}")
endif()

file(GLOB tina_package_configs LIST_DIRECTORIES FALSE
    "${tina_relocated_prefix}/*/cmake/Tina/TinaConfig.cmake")
list(LENGTH tina_package_configs tina_package_config_count)
if(NOT tina_package_config_count EQUAL 1)
    message(FATAL_ERROR
        "Expected exactly one relocated TinaConfig.cmake under ${tina_relocated_prefix}, "
        "found ${tina_package_config_count}")
endif()

get_filename_component(tina_package_directory "${tina_package_configs}" DIRECTORY)
file(GLOB tina_package_files LIST_DIRECTORIES FALSE "${tina_package_directory}/*.cmake")
if(NOT tina_package_files)
    message(FATAL_ERROR "Relocated Tina package contains no CMake package files")
endif()

set(tina_forbidden_paths "${tina_original_prefix}")
if(DEFINED TINA_FORBIDDEN_BUILD_DIR AND NOT TINA_FORBIDDEN_BUILD_DIR STREQUAL "")
    cmake_path(ABSOLUTE_PATH TINA_FORBIDDEN_BUILD_DIR NORMALIZE OUTPUT_VARIABLE tina_forbidden_build_dir)
    list(APPEND tina_forbidden_paths "${tina_forbidden_build_dir}")
endif()
if(DEFINED TINA_FORBIDDEN_SOURCE_DIR AND NOT TINA_FORBIDDEN_SOURCE_DIR STREQUAL "")
    cmake_path(ABSOLUTE_PATH TINA_FORBIDDEN_SOURCE_DIR NORMALIZE OUTPUT_VARIABLE tina_forbidden_source_dir)
    list(APPEND tina_forbidden_paths "${tina_forbidden_source_dir}")
endif()
list(REMOVE_DUPLICATES tina_forbidden_paths)

foreach(tina_package_file IN LISTS tina_package_files)
    file(READ "${tina_package_file}" tina_package_content)
    string(REPLACE "\\" "/" tina_package_content "${tina_package_content}")
    if(CMAKE_HOST_WIN32)
        string(TOLOWER "${tina_package_content}" tina_package_content)
    endif()

    foreach(tina_forbidden_path IN LISTS tina_forbidden_paths)
        string(REPLACE "\\" "/" tina_normalized_forbidden_path "${tina_forbidden_path}")
        if(CMAKE_HOST_WIN32)
            string(TOLOWER "${tina_normalized_forbidden_path}" tina_normalized_forbidden_path)
        endif()
        string(FIND "${tina_package_content}" "${tina_normalized_forbidden_path}" tina_path_offset)
        if(NOT tina_path_offset EQUAL -1)
            get_filename_component(tina_package_file_name "${tina_package_file}" NAME)
            message(FATAL_ERROR
                "Relocated package file ${tina_package_file_name} leaks forbidden path "
                "${tina_forbidden_path}")
        endif()
    endforeach()
endforeach()

list(LENGTH tina_package_files tina_package_file_count)
message(STATUS
    "Verified relocated Tina SDK package: ${tina_package_file_count} CMake files, "
    "prefix=${tina_relocated_prefix}")
