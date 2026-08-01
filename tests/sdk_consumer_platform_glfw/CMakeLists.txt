cmake_minimum_required(VERSION 3.25)
project(TinaSdkPlatformGlfwConsumer LANGUAGES CXX)

find_package(Tina 0.0.1 CONFIG REQUIRED COMPONENTS PlatformGlfw)

if(NOT Tina_VERSION VERSION_EQUAL "0.0.1")
    message(FATAL_ERROR "Expected Tina package version 0.0.1, found '${Tina_VERSION}'")
endif()
if(NOT TARGET Tina::PlatformGlfw)
    message(FATAL_ERROR "The Tina package does not provide Tina::PlatformGlfw")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../sdk_consumer/VerifyInstalledTargets.cmake")
tina_verify_installed_targets(${Tina_PACKAGE_TARGETS})
tina_verify_backend_neutral_game_sdk()

if(NOT "Tina::PlatformGlfw" IN_LIST Tina_ADAPTER_TARGETS)
    message(FATAL_ERROR "Tina_ADAPTER_TARGETS does not declare Tina::PlatformGlfw")
endif()

add_executable(tina_sdk_platform_glfw_consumer main.cpp)
target_compile_features(tina_sdk_platform_glfw_consumer PRIVATE cxx_std_23)
target_link_libraries(tina_sdk_platform_glfw_consumer PRIVATE Tina::PlatformGlfw)
set_target_properties(tina_sdk_platform_glfw_consumer PROPERTIES CXX_EXTENSIONS OFF)
