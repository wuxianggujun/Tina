cmake_minimum_required(VERSION 3.28)
project(Tina)

option(TINA_BUILD_EXAMPLES "Whether or not to build examples with this stack" "${PROJECT_IS_TOP_LEVEL}")
option(TINA_BUILD_DOCS "Whether or not to generate documentation" "${PROJECT_IS_TOP_LEVEL}")

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "DEBUG")
endif ()


find_package(Git)
if (NOT GIT_FOUND)
    message(FATAL_ERROR "Git needs to be installed to pull the dependencies")
else ()
    string(REGEX MATCH "^[0-9]+[.][0-9]+" GIT_VERSION "${GIT_VERSION_STRING}")
    if ("${GIT_VERSION}" LESS "2.35")
        message(FATAL_ERROR "Git version of 2.35 is needed for patch application")
    endif ()
endif ()

# Options
include(CMakeDependentOption)

include(cmake/utils.cmake)

add_subdirectory(3rdparty)

add_shaders_directory(shaders SHADERS_TARGET_NAME)

add_executable(${PROJECT_NAME} src/main.cpp engine/base/Log.cpp engine/base/Log.hpp
        engine/base/Util.cpp
        engine/base/Util.hpp
        engine/base/PlatformDetection.hpp)


target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(${PROJECT_NAME} PUBLIC glfw bx bimg bgfx)
target_link_libraries(${PROJECT_NAME} PUBLIC Boost::utility Boost::core Boost::detail Boost::lexical_cast)
target_link_libraries(${PROJECT_NAME} PUBLIC Microsoft.GSL::GSL)
target_link_libraries(${PROJECT_NAME} PUBLIC glm)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog)
target_link_libraries(${PROJECT_NAME} PUBLIC stb::image)

target_link_libraries(${PROJECT_NAME} PUBLIC "${SHADERS_TARGET_NAME}")

target_compile_definitions(${PROJECT_NAME} PUBLIC GLFW_INCLUDE_NONE)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)

if (${TINA_BUILD_DOCS})
    set(WARNING_OPTIONS)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        list(APPEND WARNING_OPTIONS "-Wall")
        list(APPEND WARNING_OPTIONS "-Wextra")
        list(APPEND WARNING_OPTIONS "-Wno-c++98-compat")
        list(APPEND WARNING_OPTIONS "-Wdocumentation")
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        list(APPEND WARNING_OPTIONS "-Wall")
        list(APPEND WARNING_OPTIONS "-Wextra")
        list(APPEND WARNING_OPTIONS "-Wno-unknown-pragmas")
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        list(APPEND WARNING_OPTIONS "/W4")
    endif ()
    target_compile_options(${PROJECT_NAME} PRIVATE ${WARNING_OPTIONS})
    set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_WARNING_AS_ERROR FALSE)
endif ()