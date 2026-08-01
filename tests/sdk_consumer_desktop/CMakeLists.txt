cmake_minimum_required(VERSION 3.25)
project(TinaSdkDesktopBootstrapConsumer LANGUAGES CXX)

# A consumer may use its own bgfx package for unrelated code. Tina's bundled
# RenderBgfx closure must remain independent from those conventional names.
add_library(bgfx::bgfx INTERFACE IMPORTED)
add_library(bgfx::bx INTERFACE IMPORTED)
add_library(bgfx::bimg INTERFACE IMPORTED)

find_package(Tina 0.0.1 CONFIG REQUIRED COMPONENTS DesktopBootstrap)

if(NOT Tina_VERSION VERSION_EQUAL "0.0.1")
    message(FATAL_ERROR "Expected Tina package version 0.0.1, found '${Tina_VERSION}'")
endif()

foreach(tina_component IN ITEMS PlatformGlfw RenderBgfx DesktopBootstrap)
    if(NOT Tina_${tina_component}_FOUND)
        message(FATAL_ERROR "DesktopBootstrap did not mark prerequisite ${tina_component} as found")
    endif()
endforeach()

foreach(tina_target IN ITEMS Tina::DesktopBootstrap Tina::PlatformGlfw Tina::RenderBgfx)
    if(NOT TARGET ${tina_target})
        message(FATAL_ERROR "The DesktopBootstrap component is missing required target ${tina_target}")
    endif()
    if(NOT tina_target IN_LIST Tina_DESKTOP_BOOTSTRAP_TARGETS)
        message(FATAL_ERROR "Tina_DESKTOP_BOOTSTRAP_TARGETS does not declare ${tina_target}")
    endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/../sdk_consumer/VerifyInstalledTargets.cmake")
tina_verify_installed_targets(${Tina_PACKAGE_TARGETS})
tina_verify_backend_neutral_game_sdk()

foreach(tina_adapter IN ITEMS Tina::PlatformGlfw Tina::RenderBgfx)
    if(NOT tina_adapter IN_LIST Tina_ADAPTER_TARGETS)
        message(FATAL_ERROR "Tina_ADAPTER_TARGETS does not declare ${tina_adapter}")
    endif()
endforeach()

foreach(tina_bgfx_runtime_target IN ITEMS
        TinaBgfxRuntime::bgfx
        TinaBgfxRuntime::bx
        TinaBgfxRuntime::bimg)
    if(NOT TARGET ${tina_bgfx_runtime_target})
        message(FATAL_ERROR "DesktopBootstrap is missing bundled target ${tina_bgfx_runtime_target}")
    endif()
endforeach()

get_target_property(tina_render_bgfx_links Tina::RenderBgfx INTERFACE_LINK_LIBRARIES)
foreach(tina_bgfx_runtime_target IN ITEMS TinaBgfxRuntime::bgfx TinaBgfxRuntime::bx)
    if(NOT tina_render_bgfx_links MATCHES "${tina_bgfx_runtime_target}")
        message(FATAL_ERROR "RenderBgfx does not link bundled target ${tina_bgfx_runtime_target}")
    endif()
endforeach()

if(TARGET Tina::UIFreetype)
    if(NOT Tina_UIFreetype_FOUND OR
       NOT "Tina::UIFreetype" IN_LIST Tina_ADAPTER_TARGETS OR
       NOT "Tina::UIFreetype" IN_LIST Tina_DESKTOP_BOOTSTRAP_TARGETS OR
       NOT "Tina::UIFreetype" IN_LIST Tina_PACKAGE_TARGETS)
        message(FATAL_ERROR "DesktopBootstrap did not declare its installed UIFreetype closure")
    endif()
elseif(Tina_UIFreetype_FOUND)
    message(FATAL_ERROR "UIFreetype is marked found without an installed target")
endif()

add_executable(tina_sdk_desktop_bootstrap_consumer main.cpp)
target_compile_features(tina_sdk_desktop_bootstrap_consumer PRIVATE cxx_std_23)
target_link_libraries(tina_sdk_desktop_bootstrap_consumer PRIVATE Tina::DesktopBootstrap)
set_target_properties(tina_sdk_desktop_bootstrap_consumer PROPERTIES CXX_EXTENSIONS OFF)
