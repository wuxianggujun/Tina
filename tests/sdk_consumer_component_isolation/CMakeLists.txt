cmake_minimum_required(VERSION 3.25)
project(TinaSdkComponentIsolation LANGUAGES NONE)

foreach(tina_dependency IN ITEMS glfw3 bgfx Freetype)
    if(NOT CMAKE_DISABLE_FIND_PACKAGE_${tina_dependency})
        message(FATAL_ERROR "The isolation probe must disable dependency ${tina_dependency}")
    endif()
endforeach()

find_package(Tina 0.0.1 CONFIG REQUIRED COMPONENTS GameSDK)

foreach(tina_component IN ITEMS PlatformGlfw RenderBgfx UIFreetype DesktopBootstrap)
    if(Tina_${tina_component}_FOUND OR TARGET Tina::${tina_component})
        message(FATAL_ERROR "Unrequested Tina component ${tina_component} leaked into the GameSDK-only consumer")
    endif()
endforeach()
if(Tina_ADAPTER_TARGETS OR Tina_DESKTOP_BOOTSTRAP_TARGETS)
    message(FATAL_ERROR "Unrequested Tina Desktop adapter state leaked into the GameSDK-only consumer")
endif()

foreach(third_party_target IN ITEMS
        glfw
        glfw3
        bgfx::bgfx
        bgfx::bx
        bgfx::bimg
        TinaBgfxRuntime::bgfx
        TinaBgfxRuntime::bx
        TinaBgfxRuntime::bimg
        Freetype::Freetype)
    if(TARGET ${third_party_target})
        message(FATAL_ERROR "Unrequested third-party target ${third_party_target} leaked into the GameSDK-only consumer")
    endif()
endforeach()
