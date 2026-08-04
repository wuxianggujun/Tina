cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED TINA_EXPECT_TRACE_TRACY)
    message(FATAL_ERROR "TINA_EXPECT_TRACE_TRACY must describe the producer trace backend")
endif()

if(TINA_EXPECT_TRACE_TRACY)
    if(CMAKE_DISABLE_FIND_PACKAGE_Threads)
        message(FATAL_ERROR "The Tracy Profile isolation probe must allow the required Threads closure")
    endif()
    project(TinaSdkComponentIsolation LANGUAGES CXX)
else()
    project(TinaSdkComponentIsolation LANGUAGES NONE)
endif()

set(tina_disabled_dependencies glfw3 bgfx Freetype miniaudio Vorbis Opus OpusFile)
if(NOT TINA_EXPECT_TRACE_TRACY)
    list(APPEND tina_disabled_dependencies Threads)
endif()
foreach(tina_dependency IN LISTS tina_disabled_dependencies)
    if(NOT CMAKE_DISABLE_FIND_PACKAGE_${tina_dependency})
        message(FATAL_ERROR "The isolation probe must disable dependency ${tina_dependency}")
    endif()
endforeach()

find_package(Tina 0.0.1 CONFIG REQUIRED COMPONENTS GameSDK)

foreach(tina_component IN ITEMS PlatformGlfw RenderBgfx UIFreetype AudioMiniaudio DesktopBootstrap)
    if(Tina_${tina_component}_FOUND OR TARGET Tina::${tina_component})
        message(FATAL_ERROR "Unrequested Tina component ${tina_component} leaked into the GameSDK-only consumer")
    endif()
endforeach()
if(Tina_ADAPTER_TARGETS OR Tina_DESKTOP_BOOTSTRAP_TARGETS)
    message(FATAL_ERROR "Unrequested Tina Desktop adapter state leaked into the GameSDK-only consumer")
endif()
if(Tina_TraceTracy_FOUND)
    message(FATAL_ERROR "Tina::TraceTracy must remain a Core link closure, not a requestable component")
endif()

if(TINA_EXPECT_TRACE_TRACY)
    foreach(trace_target IN ITEMS Tina::TraceTracy Tracy::TracyClient Threads::Threads)
        if(NOT TARGET ${trace_target})
            message(FATAL_ERROR "The Tracy Profile Core closure is missing target ${trace_target}")
        endif()
    endforeach()
else()
    foreach(trace_target IN ITEMS Tina::TraceTracy Tracy::TracyClient Threads::Threads)
        if(TARGET ${trace_target})
            message(FATAL_ERROR "The None Core closure unexpectedly exposes target ${trace_target}")
        endif()
    endforeach()
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
        Freetype::Freetype
        Vorbis::vorbis
        Vorbis::vorbisfile
        Opus::opus
        OpusFile::opusfile)
    if(TARGET ${third_party_target})
        message(FATAL_ERROR "Unrequested third-party target ${third_party_target} leaked into the GameSDK-only consumer")
    endif()
endforeach()
