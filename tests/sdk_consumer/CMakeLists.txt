cmake_minimum_required(VERSION 3.25)
project(TinaSdkConsumer LANGUAGES CXX)

find_package(Tina 0.0.1 CONFIG REQUIRED COMPONENTS GameSDK)

if(NOT Tina_VERSION VERSION_EQUAL "0.0.1")
    message(FATAL_ERROR "Expected Tina package version 0.0.1, found '${Tina_VERSION}'")
endif()
if(NOT TARGET Tina::GameSDK)
    message(FATAL_ERROR "The Tina package does not provide Tina::GameSDK")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/VerifyInstalledTargets.cmake")
tina_verify_installed_targets(${Tina_PACKAGE_TARGETS})
tina_verify_backend_neutral_game_sdk()

if(Tina_PlatformGlfw_FOUND OR Tina_RenderBgfx_FOUND OR Tina_UIFreetype_FOUND OR
   Tina_DesktopBootstrap_FOUND OR Tina_ADAPTER_TARGETS OR Tina_DESKTOP_BOOTSTRAP_TARGETS)
    message(FATAL_ERROR "The Null Tina package unexpectedly exposes Desktop adapter state")
endif()
if(DEFINED TINA_EXPECTED_INSTALL_PREFIX AND NOT TINA_EXPECTED_INSTALL_PREFIX STREQUAL "")
    foreach(tina_adapter_header IN ITEMS
            "include/tina/platform/glfw/GlfwPlatformFactory.hpp"
            "include/tina/ui/text/FreeTypeTextRasterizerFactory.hpp"
            "include/tina/desktop/DesktopEngine.hpp")
        if(EXISTS "${TINA_EXPECTED_INSTALL_PREFIX}/${tina_adapter_header}")
            message(FATAL_ERROR "The Null Tina package unexpectedly installs ${tina_adapter_header}")
        endif()
    endforeach()
endif()

add_executable(tina_sdk_consumer main.cpp)
target_compile_features(tina_sdk_consumer PRIVATE cxx_std_23)
target_link_libraries(tina_sdk_consumer PRIVATE Tina::GameSDK)
set_target_properties(tina_sdk_consumer PROPERTIES CXX_EXTENSIONS OFF)

if(WIN32 AND TARGET Tracy::TracyClient)
    add_custom_command(TARGET tina_sdk_consumer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Tracy::TracyClient>"
            "$<TARGET_FILE_DIR:tina_sdk_consumer>/$<TARGET_FILE_NAME:Tracy::TracyClient>"
        VERBATIM
    )
endif()
