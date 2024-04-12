cmake_minimum_required(VERSION 3.26)
project(Tina)

set(CMAKE_CXX_STANDARD 17)

option(TINA_BUILD_EXAMPLES "Whether or not to build examples with this stack" "${PROJECT_IS_TOP_LEVEL}")
option(TINA_BUILD_DOCS "Whether or not to generate documentation" "${PROJECT_IS_TOP_LEVEL}")
option(TINA_BUILD_TESTING "Google Tests" ON)
option(TINA_AUTOUPDATE_SUBMODULE "Auto Update Github Submodules" ON)
option(TINA_FILE_EXCLUSION "Enable excluding log file" OFF)

# Include cmake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "DEBUG")
endif ()

if(MSVC)
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

file(GLOB_RECURSE TINA_HEADER_FILES CONFIGURE_DEPENDS engine/*.h engine/*.hpp)
file(GLOB_RECURSE TINA_SOURCE_FILES CONFIGURE_DEPENDS engine/*.cpp engine/*.c)
set(TINA_All_FIlE ${TINA_HEADER_FILES} ${TINA_SOURCE_FILES})

if(${TINA_FILE_EXCLUSION})
list(FILTER TINA_All_FIlE EXCLUDE REGEX "^${CMAKE_CURRENT_SOURCE_DIR}/engine/log/")
message(STATUS "TINA_All_FIlE: ${TINA_All_FIlE}")
endif()

set(ENGINE_SOURCES "" PARENT_SCOPE)

enable_testing()
add_subdirectory(3rdparty)
add_subdirectory(tests)

add_shaders_directory(shaders SHADERS_TARGET_NAME)

if(MSVC)
    add_compile_options("/source-charset:utf-8")
    #add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
    #add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
endif()

message(STATUS "TINA_All_FIlE: ${TINA_All_FIlE}")

add_executable(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE 
        ${TINA_All_FIlE}
        src/main.cpp
        src/TestGlfwWindow.hpp)


target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/engine")
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(${PROJECT_NAME} PUBLIC glfw bx bimg bgfx)
target_link_libraries(${PROJECT_NAME} PUBLIC EnTT::EnTT)
target_link_libraries(${PROJECT_NAME} PUBLIC Boost::predef Boost::utility Boost::core Boost::detail Boost::lexical_cast Boost::exception Boost::timer)
target_link_libraries(${PROJECT_NAME} PUBLIC Microsoft.GSL::GSL)
target_link_libraries(${PROJECT_NAME} PUBLIC glm)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog)
target_link_libraries(${PROJECT_NAME} PUBLIC stb::image)

target_link_libraries(${PROJECT_NAME} PUBLIC "${SHADERS_TARGET_NAME}")

target_compile_definitions(${PROJECT_NAME} PUBLIC GLFW_INCLUDE_NONE)
#target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)


if (${TINA_BUILD_DOCS})
include(cmake/CompileOptions.cmake)
endif ()
