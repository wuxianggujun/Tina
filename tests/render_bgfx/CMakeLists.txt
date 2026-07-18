add_executable(tina_render_bgfx_tests
    BgfxRenderDeviceFactoryTests.cpp
    BgfxSurfaceFramePlannerTests.cpp
    BgfxUIDisplayGeometryTests.cpp
)

target_compile_features(tina_render_bgfx_tests PRIVATE cxx_std_23)
target_include_directories(tina_render_bgfx_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/integration
    ${PROJECT_SOURCE_DIR}/src/render/bgfx
)
target_link_libraries(tina_render_bgfx_tests PRIVATE
    Tina::ProjectOptions
    Tina::RenderBgfx
    Tina::WindowSurfaceIntegration
    GTest::gtest_main
)
set_target_properties(tina_render_bgfx_tests PROPERTIES CXX_EXTENSIONS OFF)
