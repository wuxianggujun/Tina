include_guard(GLOBAL)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

function(tina_configure_game_sdk_target target export_name)
    set_target_properties(${target} PROPERTIES
        EXPORT_NAME ${export_name}
        INTERFACE_INCLUDE_DIRECTORIES
            "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>;$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    )
endfunction()

function(tina_configure_game_sdk_package)
    add_library(tina_game_sdk INTERFACE)
    add_library(Tina::GameSDK ALIAS tina_game_sdk)
    target_compile_features(tina_game_sdk INTERFACE cxx_std_23)
    target_link_libraries(tina_game_sdk INTERFACE
        Tina::Core
        Tina::Platform
        Tina::Task
        Tina::Render
        Tina::Runtime
        Tina::Scene
        Tina::Navigation2D
        Tina::AssetFormat
        Tina::Asset
        Tina::UI
        Tina::Audio
    )
    set_target_properties(tina_game_sdk PROPERTIES EXPORT_NAME GameSDK)

    set(tina_sdk_export_targets
        tina_game_sdk
        tina_core
        tina_platform
        tina_task
        tina_render
        tina_runtime
        tina_scene
        tina_navigation2d
        tina_asset_format
        tina_editor
        tina_asset_types
        tina_asset
        tina_ui
        tina_audio
        tina_window_surface_integration
        tina_ui_render_integration
    )

    tina_configure_game_sdk_target(tina_core Core)
    tina_configure_game_sdk_target(tina_platform Platform)
    tina_configure_game_sdk_target(tina_task Task)
    tina_configure_game_sdk_target(tina_render Render)
    tina_configure_game_sdk_target(tina_runtime Runtime)
    tina_configure_game_sdk_target(tina_scene Scene)
    tina_configure_game_sdk_target(tina_navigation2d Navigation2D)
    tina_configure_game_sdk_target(tina_asset_format AssetFormat)
    tina_configure_game_sdk_target(tina_editor Editor)
    tina_configure_game_sdk_target(tina_asset_types AssetTypes)
    tina_configure_game_sdk_target(tina_asset Asset)
    tina_configure_game_sdk_target(tina_ui UI)
    tina_configure_game_sdk_target(tina_audio Audio)
    tina_configure_game_sdk_target(tina_window_surface_integration WindowSurfaceIntegration)
    tina_configure_game_sdk_target(tina_ui_render_integration UIRenderIntegration)

    set(TINA_PACKAGE_WITH_PHYSICS2D OFF)
    if(TARGET tina_physics2d)
        list(APPEND tina_sdk_export_targets tina_physics2d)
        tina_configure_game_sdk_target(tina_physics2d Physics2D)
        set(TINA_PACKAGE_WITH_PHYSICS2D ON)
    endif()

    set(TINA_PACKAGE_WITH_UI_UIA OFF)
    if(TARGET tina_ui_uia)
        list(APPEND tina_sdk_export_targets tina_ui_uia)
        tina_configure_game_sdk_target(tina_ui_uia UIUia)
        set(TINA_PACKAGE_WITH_UI_UIA ON)
    endif()

    set(TINA_PACKAGE_WITH_TRACE_TRACY OFF)
    if(TARGET tina_trace_tracy)
        list(APPEND tina_sdk_export_targets tina_trace_tracy)
        tina_configure_game_sdk_target(tina_trace_tracy TraceTracy)
        set(TINA_PACKAGE_WITH_TRACE_TRACY ON)
    endif()

    set(TINA_PACKAGE_WITH_PLATFORM_GLFW OFF)
    if(TARGET tina_platform_glfw)
        tina_configure_game_sdk_target(tina_platform_glfw PlatformGlfw)
        set(TINA_PACKAGE_WITH_PLATFORM_GLFW ON)
    endif()

    set(TINA_PACKAGE_WITH_RENDER_BGFX OFF)
    if(TARGET tina_render_bgfx)
        tina_configure_game_sdk_target(tina_render_bgfx RenderBgfx)
        set(TINA_PACKAGE_WITH_RENDER_BGFX ON)
    endif()

    set(TINA_PACKAGE_WITH_UI_FREETYPE OFF)
    if(TARGET tina_ui_freetype)
        tina_configure_game_sdk_target(tina_ui_freetype UIFreetype)
        set(TINA_PACKAGE_WITH_UI_FREETYPE ON)
    endif()

    set(TINA_PACKAGE_WITH_AUDIO_MINIAUDIO OFF)
    set(TINA_PACKAGE_AUDIO_MINIAUDIO_NEEDS_THREADS OFF)
    set(TINA_PACKAGE_AUDIO_MINIAUDIO_WITH_LIBVORBIS OFF)
    set(TINA_PACKAGE_AUDIO_MINIAUDIO_WITH_LIBOPUS OFF)
    if(TARGET tina_audio_miniaudio)
        tina_configure_game_sdk_target(tina_audio_miniaudio AudioMiniaudio)
        set(TINA_PACKAGE_WITH_AUDIO_MINIAUDIO ON)
        if(UNIX AND NOT APPLE)
            set(TINA_PACKAGE_AUDIO_MINIAUDIO_NEEDS_THREADS ON)
        endif()
        if(TINA_AUDIO_ENABLE_LIBVORBIS)
            set(TINA_PACKAGE_AUDIO_MINIAUDIO_WITH_LIBVORBIS ON)
        endif()
        if(TINA_AUDIO_ENABLE_LIBOPUS)
            set(TINA_PACKAGE_AUDIO_MINIAUDIO_WITH_LIBOPUS ON)
        endif()
    endif()

    set(TINA_PACKAGE_WITH_DESKTOP_BOOTSTRAP OFF)
    if(TARGET tina_bootstrap_desktop)
        tina_configure_game_sdk_target(tina_bootstrap_desktop DesktopBootstrap)
        set(TINA_PACKAGE_WITH_DESKTOP_BOOTSTRAP ON)
    endif()

    install(TARGETS ${tina_sdk_export_targets}
        EXPORT TinaTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    if(TARGET tina_platform_glfw)
        install(TARGETS tina_platform_glfw
            EXPORT TinaPlatformGlfwTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()
    if(TARGET tina_render_bgfx)
        if(NOT TARGET bgfx OR NOT TARGET bx OR NOT TARGET bimg)
            message(FATAL_ERROR "RenderBgfx packaging requires bgfx, bx, and bimg targets")
        endif()

        install(TARGETS tina_render_bgfx
            EXPORT TinaRenderBgfxTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )

        # Export only the libraries required by the installed RenderBgfx
        # target. Upstream BGFX_INSTALL also installs offline shader and image
        # tools that are not part of Tina's runtime SDK.
        install(TARGETS bgfx bx bimg
            EXPORT TinaBgfxTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )

        if(MINGW)
            set(tina_bx_compat_platform mingw)
        elseif(WIN32)
            set(tina_bx_compat_platform msvc)
        elseif(APPLE)
            set(tina_bx_compat_platform osx)
        elseif(UNIX)
            set(tina_bx_compat_platform linux)
        else()
            message(FATAL_ERROR "Unsupported bx compatibility header platform")
        endif()

        install(DIRECTORY "${BGFX_DIR}/include/bgfx"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        )
        install(DIRECTORY "${BX_DIR}/include/bx"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        )
        install(DIRECTORY "${BX_DIR}/include/compat/${tina_bx_compat_platform}"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/bx/compat"
        )
        install(DIRECTORY "${BX_DIR}/include/tinystl"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/bx"
        )
        install(DIRECTORY "${BIMG_DIR}/include/bimg"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        )
        install(FILES
            "${BGFX_DIR}/LICENSE"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/Tina/bgfx"
        )
        install(FILES
            "${BX_DIR}/LICENSE"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/Tina/bx"
        )
        install(FILES
            "${BIMG_DIR}/LICENSE"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/Tina/bimg"
        )
        install(FILES
            "${BIMG_DIR}/3rdparty/astc-encoder/LICENSE.txt"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/Tina/astc-encoder"
        )
        install(FILES
            "${BIMG_DIR}/3rdparty/tinyexr/deps/miniz/LICENSE"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/Tina/miniz"
        )
    endif()
    if(TARGET tina_ui_freetype)
        install(TARGETS tina_ui_freetype
            EXPORT TinaUIFreetypeTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()
    if(TARGET tina_audio_miniaudio)
        install(TARGETS tina_audio_miniaudio
            EXPORT TinaAudioMiniaudioTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()
    if(TARGET tina_bootstrap_desktop)
        install(TARGETS tina_bootstrap_desktop
            EXPORT TinaDesktopBootstrapTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()
    install(DIRECTORY
        "${PROJECT_SOURCE_DIR}/include/tina/core"
        "${PROJECT_SOURCE_DIR}/include/tina/platform"
        "${PROJECT_SOURCE_DIR}/include/tina/task"
        "${PROJECT_SOURCE_DIR}/include/tina/render"
        "${PROJECT_SOURCE_DIR}/include/tina/runtime"
        "${PROJECT_SOURCE_DIR}/include/tina/scene"
        "${PROJECT_SOURCE_DIR}/include/tina/navigation2d"
        "${PROJECT_SOURCE_DIR}/include/tina/asset_format"
        "${PROJECT_SOURCE_DIR}/include/tina/editor"
        "${PROJECT_SOURCE_DIR}/include/tina/asset"
        "${PROJECT_SOURCE_DIR}/include/tina/ui"
        "${PROJECT_SOURCE_DIR}/include/tina/audio"
        "${PROJECT_SOURCE_DIR}/include/tina/integration"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tina"
        FILES_MATCHING PATTERN "*.hpp"
        PATTERN "glfw" EXCLUDE
        PATTERN "AudioDecode.hpp" EXCLUDE
        PATTERN "miniaudio" EXCLUDE
        PATTERN "FreeTypeTextRasterizerFactory.hpp" EXCLUDE
        PATTERN "WindowsUiaAccessibilityProviderFactory.hpp" EXCLUDE
        PATTERN "TileMapPhysicsSync.hpp" EXCLUDE
    )
    if(TARGET tina_physics2d)
        install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/tina/physics2d"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tina"
            FILES_MATCHING PATTERN "*.hpp"
        )
        install(FILES "${PROJECT_SOURCE_DIR}/include/tina/asset/TileMapPhysicsSync.hpp"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tina/asset"
        )
    endif()
    if(TARGET tina_ui_uia)
        install(FILES "${PROJECT_SOURCE_DIR}/include/tina/ui/WindowsUiaAccessibilityProviderFactory.hpp"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tina/ui"
        )
    endif()
    if(TARGET tina_platform_glfw)
        install(FILES "${PROJECT_SOURCE_DIR}/include/tina/platform/glfw/GlfwPlatformFactory.hpp"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tina/platform/glfw"
        )
    endif()
    if(TARGET tina_ui_freetype)
        install(FILES "${PROJECT_SOURCE_DIR}/include/tina/ui/text/FreeTypeTextRasterizerFactory.hpp"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tina/ui/text"
        )
    endif()
    if(TARGET tina_audio_miniaudio)
        install(FILES "${PROJECT_SOURCE_DIR}/include/tina/audio/AudioDecode.hpp"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tina/audio"
        )
        install(FILES "${PROJECT_SOURCE_DIR}/include/tina/audio/miniaudio/MiniaudioDevice.hpp"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tina/audio/miniaudio"
        )
    endif()
    if(TARGET tina_bootstrap_desktop)
        install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/tina/desktop"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tina"
            FILES_MATCHING PATTERN "*.hpp"
        )
    endif()

    set(tina_package_directory "${CMAKE_INSTALL_LIBDIR}/cmake/Tina")
    configure_package_config_file(
        "${PROJECT_SOURCE_DIR}/cmake/TinaConfig.cmake.in"
        "${PROJECT_BINARY_DIR}/TinaConfig.cmake"
        INSTALL_DESTINATION "${tina_package_directory}"
        PATH_VARS CMAKE_INSTALL_INCLUDEDIR
    )
    # ADR 0024 requires stricter semantics than CMake's built-in ExactVersion
    # template: that template ignores a tweak component and accepts the lower
    # endpoint of a version range. Keep the version file explicit so every
    # non-three-component or range request fails closed.
    configure_file(
        "${PROJECT_SOURCE_DIR}/cmake/TinaConfigVersion.cmake.in"
        "${PROJECT_BINARY_DIR}/TinaConfigVersion.cmake"
        @ONLY
    )

    install(EXPORT TinaTargets
        FILE TinaTargets.cmake
        NAMESPACE Tina::
        DESTINATION "${tina_package_directory}"
    )
    if(TARGET tina_platform_glfw)
        install(EXPORT TinaPlatformGlfwTargets
            FILE TinaPlatformGlfwTargets.cmake
            NAMESPACE Tina::
            DESTINATION "${tina_package_directory}"
        )
    endif()
    if(TARGET tina_render_bgfx)
        install(EXPORT TinaRenderBgfxTargets
            FILE TinaRenderBgfxTargets.cmake
            NAMESPACE Tina::
            DESTINATION "${tina_package_directory}"
        )
        install(EXPORT TinaBgfxTargets
            FILE TinaBgfxRuntimeTargets.cmake
            NAMESPACE TinaBgfxRuntime::
            DESTINATION "${tina_package_directory}"
        )
    endif()
    if(TARGET tina_ui_freetype)
        install(EXPORT TinaUIFreetypeTargets
            FILE TinaUIFreetypeTargets.cmake
            NAMESPACE Tina::
            DESTINATION "${tina_package_directory}"
        )
    endif()
    if(TARGET tina_audio_miniaudio)
        install(EXPORT TinaAudioMiniaudioTargets
            FILE TinaAudioMiniaudioTargets.cmake
            NAMESPACE Tina::
            DESTINATION "${tina_package_directory}"
        )
    endif()
    if(TARGET tina_bootstrap_desktop)
        install(EXPORT TinaDesktopBootstrapTargets
            FILE TinaDesktopBootstrapTargets.cmake
            NAMESPACE Tina::
            DESTINATION "${tina_package_directory}"
        )
    endif()
    install(FILES
        "${PROJECT_BINARY_DIR}/TinaConfig.cmake"
        "${PROJECT_BINARY_DIR}/TinaConfigVersion.cmake"
        DESTINATION "${tina_package_directory}"
    )
endfunction()
