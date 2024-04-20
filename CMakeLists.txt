cmake_minimum_required(VERSION 3.20..3.28)
project(Tina VERSION 0.0.1 DESCRIPTION "This is easy engine" LANGUAGES CXX)

# Set /std:c++20 for all configurations
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Cache the root directory
set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Root directory path")

set(BUILD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build CACHE PATH "Build directory path")

#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BUILD_DIR})
#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${BUILD_DIR}/lib)
#set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${BUILD_DIR}/lib)

option(TINA_BUILD_EXAMPLES "Whether or not to build examples with this stack" "${PROJECT_IS_TOP_LEVEL}")
option(TINA_BUILD_DOCS "Whether or not to generate documentation" "${PROJECT_IS_TOP_LEVEL}")
option(TINA_BUILD_TESTING "Google Tests" ON)
option(TINA_AUTOUPDATE_SUBMODULE "Auto Update Github Submodules" OFF)
option(TINA_FILE_EXCLUSION "Enable excluding log file" OFF)

# Include cmake modules
list(APPEND CMAKE_MODULE_PATH "${ROOT_DIR}/cmake")

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "DEBUG")
endif ()

if(MSVC)
    add_compile_options("/source-charset:utf-8")
    string(APPEND CMAKE_CXX_FLAGS " /Zc:__cplusplus /MP")
endif()

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
#×Ô¶¯¸üÐÂ×ÓÄ£¿é
if(TINA_AUTOUPDATE_SUBMODULE)
include(cmake/UpdateSubmodules.cmake)
endif()
include(cmake/utils.cmake)

file(GLOB_RECURSE TINA_HEADER_FILES CONFIGURE_DEPENDS
            ${ROOT_DIR}/engine/core/*.h**
            ${ROOT_DIR}/engine/log/*.h**
            ${ROOT_DIR}/engine/game/*.h**
            ${ROOT_DIR}/engine/timer/*.h**
            ${ROOT_DIR}/engine/framework/*.h**)

source_group(TINA_All_FIlE FILES ${TINA_HEADER_FILES})

file(GLOB_RECURSE TINA_SOURCE_FILES CONFIGURE_DEPENDS
            ${ROOT_DIR}/engine/core/*.c**
            ${ROOT_DIR}/engine/log/*.c**
            ${ROOT_DIR}/engine/game/*.c**
            ${ROOT_DIR}/engine/timer/*.c**
            ${ROOT_DIR}/engine/framework/*.c**)

source_group(TINA_All_FIlE FILES ${TINA_SOURCE_FILES})

set(TINA_All_FIlE ${TINA_HEADER_FILES} ${TINA_SOURCE_FILES})

if(${TINA_FILE_EXCLUSION})
list(FILTER TINA_All_FIlE EXCLUDE REGEX "^${ROOT_DIR}/engine/log/")
message(STATUS "TINA_All_FIlE: ${TINA_All_FIlE}")
endif()

set(ENGINE_SOURCES "" PARENT_SCOPE)

enable_testing()

add_subdirectory(3rdparty)
#add_subdirectory(engine/framework)
add_subdirectory(tests)

add_shaders_directory(${ROOT_DIR}/shaders SHADERS_TARGET_NAME)

message(STATUS "TINA_All_FIlE: ${TINA_All_FIlE}")

add_executable(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE 
        ${TINA_All_FIlE}
        "src/main.cpp"
        "src/TestGlfwWindow.hpp")

target_include_directories(${PROJECT_NAME} PUBLIC "${ROOT_DIR}/engine")
target_include_directories(${PROJECT_NAME} PRIVATE "${ROOT_DIR}/src")
target_link_libraries(${PROJECT_NAME} PUBLIC glfw glm bx bimg bgfx)
target_link_libraries(${PROJECT_NAME} PUBLIC Boost::predef Boost::utility Boost::core Boost::detail Boost::lexical_cast Boost::exception Boost::timer Boost::chrono)
target_link_libraries(${PROJECT_NAME} PUBLIC spdlog stb::image Microsoft.GSL::GSL EnTT::EnTT)

target_link_libraries(${PROJECT_NAME} PUBLIC "${SHADERS_TARGET_NAME}")

target_compile_definitions(${PROJECT_NAME} PUBLIC GLFW_INCLUDE_NONE)


if (${TINA_BUILD_DOCS})
include(cmake/CompileOptions.cmake)
endif ()
