set(TINA_CORE_TEST_SOURCES
    core/AssertReleaseCompileTests.cpp
    core/AssertTests.cpp
    core/EnumFlagsTests.cpp
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
    render/header_isolation/RenderSurfaceHeader.cpp
    render/NullRenderDeviceTests.cpp
    runtime/header_isolation/EngineCompositionFactoriesHeader.cpp
    runtime/header_isolation/EngineHostHeader.cpp
    runtime/header_isolation/GameApplicationHeader.cpp
    runtime/header_isolation/InputActionsHeader.cpp
    runtime/header_isolation/InputActionMapHeader.cpp
    runtime/header_isolation/PlatformEventDispatcherHeader.cpp
    runtime/header_isolation/PlatformEventsHeader.cpp
    runtime/header_isolation/WindowSurfaceIntegrationHeader.cpp
    runtime/InputActionTests.cpp
    runtime/PlatformEventDispatcherTests.cpp
    runtime/RuntimeLifecycleTests.cpp
    runtime/WindowSurfaceLeaseTests.cpp
    runtime/WindowSurfaceRuntimeTests.cpp
    task/header_isolation/TaskSystemHeader.cpp
    task/TaskSystemTests.cpp
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
    target_sources(tina_tests PRIVATE
        core/LegacyCompatibilityTests.cpp
        core/LegacyTimeTests.cpp
        engine/Camera3DTests.cpp
        engine/EventSystemTests.cpp
        engine/ResourceTests.cpp
        ui/UIActionTests.cpp
        ui/UIRuntimeTests.cpp
        ${PROJECT_SOURCE_DIR}/src/engine/EventSystem.cpp
        ${PROJECT_SOURCE_DIR}/src/engine/SubscriptionToken.cpp
        ${PROJECT_SOURCE_DIR}/src/ui/UINode.cpp
        ${PROJECT_SOURCE_DIR}/src/ui/UILayoutManager.cpp
        ${PROJECT_SOURCE_DIR}/src/ui/UIStyle.cpp
        ${PROJECT_SOURCE_DIR}/src/ui/UIScrollView.cpp
    )
    target_link_libraries(tina_tests PRIVATE
        Tina::CoreLegacy
        spdlog::spdlog
        bx
    )
endif()

if(TINA_BUILD_PLATFORM_GLFW)
    add_subdirectory(platform_glfw)
endif()

if(TINA_BUILD_RENDER_BGFX)
    add_subdirectory(render_bgfx)
endif()

add_subdirectory(vnext/ui)
add_subdirectory(vnext/runtime)
