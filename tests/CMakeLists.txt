set(TINA_CORE_TEST_SOURCES
    core/AssertReleaseCompileTests.cpp
    core/AssertTests.cpp
    core/ContentHashDigestTests.cpp
    core/ReadFileTests.cpp
    core/WriteFileTests.cpp
    core/EnumFlagsTests.cpp
    core/header_isolation/AssetIdHeader.cpp
    core/header_isolation/ContentHashHeader.cpp
    core/header_isolation/ContentHashDigestHeader.cpp
    core/header_isolation/ReadFileHeader.cpp
    core/header_isolation/WriteFileHeader.cpp
    core/header_isolation/ErrorHeader.cpp
    core/header_isolation/CountingMemoryResourceHeader.cpp
    core/header_isolation/FixedStepAccumulatorHeader.cpp
    core/header_isolation/FrameArenaHeader.cpp
    core/header_isolation/GenerationIdHeader.cpp
    core/header_isolation/GenerationPoolHeader.cpp
    core/header_isolation/MemoryTrackerHeader.cpp
    core/header_isolation/MonotonicClockHeader.cpp
    core/header_isolation/ResultHeader.cpp
    core/header_isolation/Utf8Header.cpp
    core/PublicHeaderIsolationTests.cpp
    core/ResultTests.cpp
    core/FrameArenaTests.cpp
    core/GenerationPoolTests.cpp
    core/MemoryTrackerTests.cpp
    core/ScopeExitTests.cpp
    core/TimeTests.cpp
    core/TypesTests.cpp
    core/Utf8Tests.cpp
    core/TypesWindowsAfterTests.cpp
    core/TypesWindowsBeforeTests.cpp
)

set(TINA_RUNTIME_TEST_SOURCES
    platform/header_isolation/InputHeader.cpp
    platform/header_isolation/GlfwPlatformFactoryHeader.cpp
    platform/header_isolation/PlatformBackendHeader.cpp
    platform/header_isolation/PlatformFrameHeader.cpp
    platform/header_isolation/WindowHeader.cpp
    platform/PlatformBackendTests.cpp
    render/header_isolation/RenderDeviceHeader.cpp
    render/header_isolation/RenderFrameHeader.cpp
    render/header_isolation/RenderSceneHeader.cpp
    render/header_isolation/RenderSurfaceHeader.cpp
    render/header_isolation/UIDisplayListHeader.cpp
    render/header_isolation/UploadTicketHeader.cpp
    render/header_isolation/WorldPointerSampleHeader.cpp
    render/NullRenderDeviceTests.cpp
    render/UploadTicketTests.cpp
    render/GpuTextureUploadTests.cpp
    render/UIDisplayListTests.cpp
    runtime/header_isolation/EngineCompositionFactoriesHeader.cpp
    runtime/header_isolation/EngineConfigHeader.cpp
    runtime/header_isolation/EngineHostHeader.cpp
    runtime/header_isolation/GameApplicationHeader.cpp
    runtime/header_isolation/InputActionsHeader.cpp
    runtime/header_isolation/InputActionMapHeader.cpp
    runtime/header_isolation/PlatformEventDispatcherHeader.cpp
    runtime/header_isolation/PlatformEventsHeader.cpp
    runtime/header_isolation/PrimaryWindowUIHeader.cpp
    runtime/header_isolation/WindowSurfaceIntegrationHeader.cpp
    runtime/InputActionTests.cpp
    runtime/Sample2DTileSelectionTests.cpp
    runtime/PlatformEventDispatcherTests.cpp
    runtime/RuntimeLifecycleTests.cpp
    runtime/WindowSurfaceLeaseTests.cpp
    runtime/WindowSurfaceRuntimeTests.cpp
    task/header_isolation/TaskSystemHeader.cpp
    task/header_isolation/TaskGroupHeader.cpp
    task/BoundedTaskSystemTests.cpp
    task/TaskSystemTests.cpp
)

set(TINA_LEGACY_TEST_SOURCES
    core/LegacyCompatibilityTests.cpp
    core/LegacyTimeTests.cpp
    engine/Camera3DTests.cpp
    engine/EventSystemTests.cpp
    engine/ResourceTests.cpp
    ui/UIActionTests.cpp
    ui/UIRuntimeTests.cpp
)

set(TINA_LEGACY_TEST_SUPPORT_SOURCES
    ${PROJECT_SOURCE_DIR}/src/engine/EventSystem.cpp
    ${PROJECT_SOURCE_DIR}/src/engine/SubscriptionToken.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/UINode.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/UILayoutManager.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/UIStyle.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/UIScrollView.cpp
)

add_executable(tina_tests
    ${TINA_CORE_TEST_SOURCES}
    ${TINA_RUNTIME_TEST_SOURCES}
)

set_source_files_properties(
    core/AssertReleaseCompileTests.cpp
    PROPERTIES COMPILE_DEFINITIONS NDEBUG
)

target_compile_features(tina_tests PRIVATE cxx_std_23)
target_include_directories(tina_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/tests
    ${PROJECT_SOURCE_DIR}/samples/2d_tilemap_bgfx
    ${PROJECT_SOURCE_DIR}/src/integration
)
target_link_libraries(tina_tests PRIVATE
    Tina::ProjectOptions
    Tina::Core
    Tina::Platform
    Tina::Render
    Tina::Runtime
    Tina::Task
    Tina::WindowSurfaceIntegration
    GTest::gtest_main
)
set_target_properties(tina_tests PROPERTIES CXX_EXTENSIONS OFF)

if(TINA_BUILD_LEGACY)
    add_executable(tina_legacy_tests
        ${TINA_LEGACY_TEST_SOURCES}
        ${TINA_LEGACY_TEST_SUPPORT_SOURCES}
    )
    target_compile_features(tina_legacy_tests PRIVATE cxx_std_23)
    target_include_directories(tina_legacy_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/tests
    )
    target_link_libraries(tina_legacy_tests PRIVATE
        Tina::ProjectOptions
        Tina::CoreLegacy
        spdlog::spdlog
        bx
        GTest::gtest_main
    )
    set_target_properties(tina_legacy_tests PROPERTIES CXX_EXTENSIONS OFF)
endif()

if(TINA_BUILD_PLATFORM_GLFW)
    add_subdirectory(platform_glfw)
endif()

if(TINA_BUILD_RENDER_BGFX)
    add_subdirectory(render_bgfx)
endif()

add_subdirectory(vnext/ui)
if(TINA_BUILD_UI_FREETYPE)
    add_subdirectory(vnext/ui_freetype)
endif()
add_subdirectory(vnext/scene)
if(TINA_BUILD_PHYSICS2D)
    add_subdirectory(vnext/physics2d)
endif()
add_subdirectory(vnext/asset_format)
add_subdirectory(vnext/asset)
add_subdirectory(vnext/runtime)
add_subdirectory(vnext/audio)
add_subdirectory(ui_render)
add_subdirectory(render_scene)
