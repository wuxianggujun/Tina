include_guard(GLOBAL)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

function(tina_configure_game_sdk_package)
    set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME sdk)
    set(package_directory "${CMAKE_INSTALL_LIBDIR}/cmake/Tina")
    set(installed_targets tina_game_sdk)
    configure_file("${PROJECT_SOURCE_DIR}/cmake/TinaRetireModuleArchives.cmake.in"
        "${PROJECT_BINARY_DIR}/TinaRetireModuleArchives.cmake" @ONLY)
    install(SCRIPT "${PROJECT_BINARY_DIR}/TinaRetireModuleArchives.cmake")
    set(TINA_PACKAGE_AUDIO_MINIAUDIO_NEEDS_THREADS OFF)
    if(TARGET tina_audio_miniaudio AND UNIX AND NOT APPLE)
        set(TINA_PACKAGE_AUDIO_MINIAUDIO_NEEDS_THREADS ON)
    endif()

    # One first-party archive; configuration directories prevent Debug/Release
    # installs from replacing each other's ABI-incompatible bytes.
    install(TARGETS tina_game_sdk EXPORT TinaTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/$<CONFIG>")
    install(EXPORT TinaTargets FILE TinaTargets.cmake NAMESPACE Tina::
        DESTINATION "${package_directory}")
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/Tina")

    if(TARGET tina_render_bgfx)
        # Keep third-party archives private instead of copying their contents into
        # Tina.lib. Third-party C++ headers are not part of the SDK authoring API.
        foreach(dependency IN ITEMS bgfx bx bimg)
            if(NOT TARGET ${dependency})
                message(FATAL_ERROR "Tina's renderer is missing ${dependency}")
            endif()
            get_target_property(includes ${dependency} INTERFACE_INCLUDE_DIRECTORIES)
            if(includes)
                set_property(TARGET ${dependency} PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                    "$<BUILD_INTERFACE:${includes}>")
            endif()
        endforeach()
        list(APPEND installed_targets bgfx bx bimg)
        install(TARGETS bgfx bx bimg EXPORT TinaBgfxTargets
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/$<CONFIG>"
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/$<CONFIG>"
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
        install(EXPORT TinaBgfxTargets FILE TinaBgfxRuntimeTargets.cmake
            NAMESPACE TinaBgfxRuntime:: DESTINATION "${package_directory}")
        install(FILES "${BGFX_DIR}/LICENSE"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/Tina/bgfx")
        install(FILES "${BX_DIR}/LICENSE"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/Tina/bx")
        install(FILES "${BIMG_DIR}/LICENSE"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/Tina/bimg")
        install(FILES "${BIMG_DIR}/3rdparty/astc-encoder/LICENSE.txt"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/Tina/astc-encoder")
        install(FILES "${BIMG_DIR}/3rdparty/tinyexr/deps/miniz/LICENSE"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/Tina/miniz")
        install(FILES
            "${PROJECT_SOURCE_DIR}/src/render/bgfx/shaders/tina_sprite2d.sh"
            "${PROJECT_SOURCE_DIR}/src/render/bgfx/shaders/tina_mesh3d.sh"
            "${PROJECT_SOURCE_DIR}/src/render/bgfx/shaders/tina_postprocess.sh"
            "${PROJECT_SOURCE_DIR}/src/render/bgfx/shaders/tina_alpha_mask.sh"
            "${PROJECT_SOURCE_DIR}/src/render/bgfx/shaders/tina_skin_palette.sh"
            "${PROJECT_SOURCE_DIR}/src/render/bgfx/shaders/tina_water_wave.sh"
            "${PROJECT_SOURCE_DIR}/src/render/bgfx/shaders/tina_sprite2d_fixture.def.sc"
            "${PROJECT_SOURCE_DIR}/src/render/bgfx/shaders/tina_opaque3d_mr.def.sc"
            "${PROJECT_SOURCE_DIR}/src/render/bgfx/shaders/tina_postprocess.def.sc"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/Tina/shaders")
        install(FILES "${BGFX_DIR}/src/bgfx_shader.sh" "${BGFX_DIR}/src/bgfx_compute.sh"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/Tina/shaders/bgfx")
    endif()

    install(DIRECTORY
        "${PROJECT_SOURCE_DIR}/include/tina/core"
        "${PROJECT_SOURCE_DIR}/include/tina/math"
        "${PROJECT_SOURCE_DIR}/include/tina/platform"
        "${PROJECT_SOURCE_DIR}/include/tina/task"
        "${PROJECT_SOURCE_DIR}/include/tina/save"
        "${PROJECT_SOURCE_DIR}/include/tina/gameplay"
        "${PROJECT_SOURCE_DIR}/include/tina/gameplay2d"
        "${PROJECT_SOURCE_DIR}/include/tina/ai"
        "${PROJECT_SOURCE_DIR}/include/tina/render"
        "${PROJECT_SOURCE_DIR}/include/tina/runtime"
        "${PROJECT_SOURCE_DIR}/include/tina/scene"
        "${PROJECT_SOURCE_DIR}/include/tina/animation3d"
        "${PROJECT_SOURCE_DIR}/include/tina/gameplay3d"
        "${PROJECT_SOURCE_DIR}/include/tina/navigation2d"
        "${PROJECT_SOURCE_DIR}/include/tina/navigation3d"
        "${PROJECT_SOURCE_DIR}/include/tina/localization"
        "${PROJECT_SOURCE_DIR}/include/tina/network"
        "${PROJECT_SOURCE_DIR}/include/tina/asset_format"
        "${PROJECT_SOURCE_DIR}/include/tina/asset"
        "${PROJECT_SOURCE_DIR}/include/tina/ui"
        "${PROJECT_SOURCE_DIR}/include/tina/audio"
        "${PROJECT_SOURCE_DIR}/include/tina/integration"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tina"
        FILES_MATCHING PATTERN "*.hpp"
        PATTERN "glfw" EXCLUDE
        PATTERN "android" EXCLUDE
        PATTERN "html5" EXCLUDE
        PATTERN "ios" EXCLUDE
        PATTERN "tls" EXCLUDE
        PATTERN "miniaudio" EXCLUDE
        PATTERN "AudioDecode.hpp" EXCLUDE
        PATTERN "FreeTypeTextRasterizerFactory.hpp" EXCLUDE
        PATTERN "WindowsUiaAccessibilityProviderFactory.hpp" EXCLUDE
        PATTERN "TileMapPhysicsSync.hpp" EXCLUDE
        PATTERN "PhysicsNavigationSync2D.hpp" EXCLUDE)
    install(FILES "${PROJECT_SOURCE_DIR}/thirdparty/nlohmann/LICENSE"
        "${PROJECT_SOURCE_DIR}/thirdparty/nlohmann/NOTICE.json"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/Tina/nlohmann")
    foreach(dimension IN ITEMS 2d 3d)
        if(TARGET tina_physics${dimension})
            install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/tina/physics${dimension}"
                DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tina" FILES_MATCHING PATTERN "*.hpp")
        endif()
    endforeach()
    if(TARGET tina_physics2d)
        install(FILES "${PROJECT_SOURCE_DIR}/include/tina/asset/TileMapPhysicsSync.hpp"
            "${PROJECT_SOURCE_DIR}/include/tina/asset/PhysicsNavigationSync2D.hpp"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tina/asset")
    endif()
    if(TARGET tina_ui_uia)
        install(FILES "${PROJECT_SOURCE_DIR}/include/tina/ui/WindowsUiaAccessibilityProviderFactory.hpp"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tina/ui")
    endif()
    foreach(platform IN ITEMS glfw android html5 ios)
        if(TARGET tina_platform_${platform})
            install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/tina/platform/${platform}"
                DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tina/platform" FILES_MATCHING PATTERN "*.hpp")
        endif()
    endforeach()
    if(TARGET tina_ui_freetype)
        install(FILES "${PROJECT_SOURCE_DIR}/include/tina/ui/text/FreeTypeTextRasterizerFactory.hpp"
            "${PROJECT_SOURCE_DIR}/include/tina/ui/text/TextShaper.h"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tina/ui/text")
        install(FILES "${PROJECT_SOURCE_DIR}/cmake/FindFriBidi.cmake"
            "${PROJECT_SOURCE_DIR}/cmake/FindHarfBuzz.cmake" DESTINATION "${package_directory}")
    endif()
    if(TARGET tina_network_tls)
        install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/tina/network/tls"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tina/network" FILES_MATCHING PATTERN "*.hpp")
    endif()
    if(TARGET tina_audio_miniaudio)
        install(FILES "${PROJECT_SOURCE_DIR}/include/tina/audio/AudioDecode.hpp"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tina/audio")
        install(FILES "${PROJECT_SOURCE_DIR}/include/tina/audio/miniaudio/MiniaudioDevice.hpp"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tina/audio/miniaudio")
    endif()
    if(TARGET tina_bootstrap_desktop)
        install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/tina/desktop"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tina" FILES_MATCHING PATTERN "*.hpp")
    endif()

    # Host tools are products, never objects in the runtime archive.
    set(TINA_PACKAGE_WITH_ASSETC OFF)
    if(TARGET tina_msdfgen)
        list(APPEND installed_targets tina_msdfgen)
        if(WIN32)
            install(TARGETS tina_msdfgen RUNTIME_DEPENDENCY_SET tina_font_runtime
                RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
            install(RUNTIME_DEPENDENCY_SET tina_font_runtime
                DIRECTORIES "$<TARGET_FILE_DIR:tina_msdfgen>"
                PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
                POST_EXCLUDE_REGEXES ".*[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\\\].*"
                RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
        else()
            install(TARGETS tina_msdfgen RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
        endif()
        install(FILES "${PROJECT_SOURCE_DIR}/tools/fonts/bake_ui_font.py"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/Tina/tools")
    endif()
    if(TARGET tina_assetc)
        list(APPEND installed_targets tina_assetc)
        install(TARGETS tina_assetc RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
        if(WIN32)
            install(FILES $<TARGET_RUNTIME_DLLS:tina_assetc> DESTINATION "${CMAKE_INSTALL_BINDIR}")
        endif()
        set(TINA_PACKAGE_WITH_ASSETC ON)
    endif()

    configure_package_config_file("${PROJECT_SOURCE_DIR}/cmake/TinaConfig.cmake.in"
        "${PROJECT_BINARY_DIR}/TinaConfig.cmake"
        INSTALL_DESTINATION "${package_directory}"
        PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_BINDIR CMAKE_INSTALL_DATAROOTDIR)
    configure_file("${PROJECT_SOURCE_DIR}/cmake/TinaConfigVersion.cmake.in"
        "${PROJECT_BINARY_DIR}/TinaConfigVersion.cmake" @ONLY)
    install(FILES "${PROJECT_BINARY_DIR}/TinaConfig.cmake"
        "${PROJECT_BINARY_DIR}/TinaConfigVersion.cmake" DESTINATION "${package_directory}")
    install(FILES
        "${PROJECT_SOURCE_DIR}/cmake/TinaGameProject.cmake"
        "${PROJECT_SOURCE_DIR}/cmake/TinaCookCatalog.cmake"
        "${PROJECT_SOURCE_DIR}/cmake/TinaProductInstall.cmake"
        "${PROJECT_SOURCE_DIR}/cmake/TinaRuntimeDependencies.cmake"
        "${PROJECT_SOURCE_DIR}/cmake/TinaNewProject.cmake"
        DESTINATION "${package_directory}")
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/templates/game-project"
        DESTINATION "${package_directory}/templates")
    configure_file("${PROJECT_SOURCE_DIR}/cmake/TinaWriteSdkManifest.cmake.in"
        "${PROJECT_BINARY_DIR}/TinaWriteSdkManifest.cmake" @ONLY)
    install(SCRIPT "${PROJECT_BINARY_DIR}/TinaWriteSdkManifest.cmake")
    add_custom_target(tina_sdk_install_artifacts)
    add_dependencies(tina_sdk_install_artifacts ${installed_targets})
endfunction()
