set(TINA_CORE_TEST_SOURCES
    core/ApplicationPathsTests.cpp
    core/ArgParserTests.cpp
    core/AssertReleaseCompileTests.cpp
    core/AssertTests.cpp
    core/CancellationSignalTests.cpp
    core/ContentHashDigestTests.cpp
    core/CrashHandlerTests.cpp
    core/DiagnosticsTests.cpp
    core/ReadFileTests.cpp
    core/UserPathsTests.cpp
    core/WriteFileTests.cpp
    core/EnumFlagsTests.cpp
    core/JsonWriterTests.cpp
    core/LogFormatTests.cpp
    core/MoveOnlyFunctionTests.cpp
    core/PathUtilTests.cpp
    core/ParseFloatTests.cpp
    core/header_isolation/ApplicationPathsHeader.cpp
    core/header_isolation/ArgParserHeader.cpp
    core/header_isolation/AssetIdHeader.cpp
    core/header_isolation/CancellationSignalHeader.cpp
    core/header_isolation/ContentHashHeader.cpp
    core/header_isolation/ContentHashDigestHeader.cpp
    core/header_isolation/CrashHandlerHeader.cpp
    core/header_isolation/DiagnosticChannelHeader.cpp
    core/header_isolation/DiagnosticsHeader.cpp
    core/header_isolation/LogFormatHeader.cpp
    core/header_isolation/LogHeader.cpp
    core/header_isolation/LogLevelHeader.cpp
    core/header_isolation/LogRecordHeader.cpp
    core/header_isolation/ReadFileHeader.cpp
    core/header_isolation/UserPathsHeader.cpp
    core/header_isolation/WriteFileHeader.cpp
    core/header_isolation/ErrorHeader.cpp
    core/header_isolation/CountingMemoryResourceHeader.cpp
    core/header_isolation/FixedStepAccumulatorHeader.cpp
    core/header_isolation/FrameArenaHeader.cpp
    core/header_isolation/GenerationIdHeader.cpp
    core/header_isolation/GenerationPoolHeader.cpp
    core/header_isolation/MemoryTrackerHeader.cpp
    core/header_isolation/MonotonicClockHeader.cpp
    core/header_isolation/MoveOnlyFunctionHeader.cpp
    core/header_isolation/JsonWriterHeader.cpp
    core/header_isolation/ParseFloatHeader.cpp
    core/header_isolation/PathUtilHeader.cpp
    core/header_isolation/ResultHeader.cpp
    core/header_isolation/TraceHeader.cpp
    core/header_isolation/Utf8Header.cpp
    core/PublicHeaderIsolationTests.cpp
    core/ResultTests.cpp
    core/FrameArenaTests.cpp
    core/GenerationPoolTests.cpp
    core/MemoryTrackerTests.cpp
    core/ScopeExitTests.cpp
    core/TimeTests.cpp
    core/TraceCompileTests.cpp
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
    render/header_isolation/RenderPassSchedulerHeader.cpp
    render/header_isolation/RenderSceneHeader.cpp
    render/header_isolation/RenderSurfaceHeader.cpp
    render/header_isolation/ShadowMapExtentConfigHeader.cpp
    render/header_isolation/UIDisplayListHeader.cpp
    render/header_isolation/UploadTicketHeader.cpp
    render/header_isolation/FramePinHeader.cpp
    render/header_isolation/FrameResourceHeader.cpp
    render/header_isolation/Texture2DFrameResourceResolverHeader.cpp
    render/header_isolation/WorldPointerSampleHeader.cpp
    render/header_isolation/Camera2DProjectionHeader.cpp
    render/NullRenderDeviceTests.cpp
    render/UploadTicketTests.cpp
    render/FramePinPacketTests.cpp
    render/GpuTextureUploadTests.cpp
    render/GpuShaderUploadTests.cpp
    render/EnvironmentMapUploadTests.cpp
    render/GpuMeshUploadTests.cpp
    render/RenderPassSchedulerTests.cpp
    render/ShadowMapExtentConfigTests.cpp
    render/UIDisplayListTests.cpp
    render/VideoDecodeTests.cpp
    runtime/header_isolation/EngineCompositionFactoriesHeader.cpp
    runtime/header_isolation/EngineConfigHeader.cpp
    runtime/header_isolation/EngineHostHeader.cpp
    runtime/header_isolation/GameApplicationHeader.cpp
    runtime/header_isolation/GameSettingsHeader.cpp
    runtime/header_isolation/GameStateCommandsHeader.cpp
    runtime/header_isolation/InputActionsHeader.cpp
    runtime/header_isolation/InputActionMapHeader.cpp
    runtime/header_isolation/PlatformEventDispatcherHeader.cpp
    runtime/header_isolation/PlatformEventsHeader.cpp
    runtime/header_isolation/PrimaryWindowUIHeader.cpp
    runtime/header_isolation/WindowSurfaceIntegrationHeader.cpp
    runtime/EngineConfigValidationTests.cpp
    runtime/InputActionTests.cpp
    runtime/Sample2DAudioControlStateTests.cpp
    runtime/Sample2DTileSelectionTests.cpp
    runtime/Sample2DWindowVisibilityPacingTests.cpp
    runtime/PlatformEventDispatcherTests.cpp
    runtime/GameSettingsTests.cpp
    runtime/GameStateStackTests.cpp
    runtime/GameStateStackIntegrationTests.cpp
    runtime/RuntimeLifecycleTests.cpp
    runtime/WindowSurfaceLeaseTests.cpp
    runtime/WindowSurfaceRuntimeTests.cpp
    task/header_isolation/TaskSystemHeader.cpp
    task/header_isolation/TaskGroupHeader.cpp
    task/BoundedTaskSystemTests.cpp
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
    ${PROJECT_SOURCE_DIR}/samples/2d_tilemap_bgfx/core
    # Reaches core/io/PathUtil.hpp, which cannot live in include/tina/ because it names
    # std::filesystem::path. Keep this after ${PROJECT_SOURCE_DIR}/tests so tests/support wins.
    ${PROJECT_SOURCE_DIR}/src
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

add_subdirectory(math)
add_subdirectory(gameplay)
add_subdirectory(animation3d)

if(TINA_BUILD_PLATFORM_GLFW)
    add_subdirectory(platform_glfw)
endif()

if(ANDROID)
    add_subdirectory(platform_android)
endif()

if(TINA_BUILD_RENDER_BGFX)
    add_subdirectory(render_bgfx)
endif()

add_subdirectory(ui)
if(TINA_BUILD_UI_FREETYPE)
    add_subdirectory(ui_freetype)
endif()
if(TINA_BUILD_UI_UIA)
    add_subdirectory(ui_uia)
endif()
add_subdirectory(scene)
if(TINA_BUILD_PHYSICS2D)
    add_subdirectory(physics2d)
endif()
add_subdirectory(asset_format)
add_subdirectory(asset)
add_subdirectory(navigation2d)
add_subdirectory(network)
add_subdirectory(save)
if (TINA_BUILD_NETWORK_TLS)
    add_subdirectory(network_tls)
endif ()
add_subdirectory(runtime_ui)
add_subdirectory(audio)
if (TINA_BUILD_AUDIO_MINIAUDIO)
    add_subdirectory(audio_miniaudio)
endif ()
add_subdirectory(ui_render)
add_subdirectory(render_scene)
