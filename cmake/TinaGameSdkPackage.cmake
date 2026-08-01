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
        tina_asset_format
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
    tina_configure_game_sdk_target(tina_asset_format AssetFormat)
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

    if(TARGET tina_ui_uia)
        list(APPEND tina_sdk_export_targets tina_ui_uia)
        tina_configure_game_sdk_target(tina_ui_uia UIUia)
    endif()

    install(TARGETS ${tina_sdk_export_targets}
        EXPORT TinaTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(DIRECTORY
        "${PROJECT_SOURCE_DIR}/include/tina/core"
        "${PROJECT_SOURCE_DIR}/include/tina/platform"
        "${PROJECT_SOURCE_DIR}/include/tina/task"
        "${PROJECT_SOURCE_DIR}/include/tina/render"
        "${PROJECT_SOURCE_DIR}/include/tina/runtime"
        "${PROJECT_SOURCE_DIR}/include/tina/scene"
        "${PROJECT_SOURCE_DIR}/include/tina/asset_format"
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

    set(tina_package_directory "${CMAKE_INSTALL_LIBDIR}/cmake/Tina")
    configure_package_config_file(
        "${PROJECT_SOURCE_DIR}/cmake/TinaConfig.cmake.in"
        "${PROJECT_BINARY_DIR}/TinaConfig.cmake"
        INSTALL_DESTINATION "${tina_package_directory}"
        PATH_VARS CMAKE_INSTALL_INCLUDEDIR
    )
    write_basic_package_version_file(
        "${PROJECT_BINARY_DIR}/TinaConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY SameMajorVersion
    )

    install(EXPORT TinaTargets
        FILE TinaTargets.cmake
        NAMESPACE Tina::
        DESTINATION "${tina_package_directory}"
    )
    install(FILES
        "${PROJECT_BINARY_DIR}/TinaConfig.cmake"
        "${PROJECT_BINARY_DIR}/TinaConfigVersion.cmake"
        DESTINATION "${tina_package_directory}"
    )
endfunction()
