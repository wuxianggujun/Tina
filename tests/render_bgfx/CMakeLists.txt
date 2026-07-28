add_library(tina_render_bgfx_ui_atlas_contract_test_object OBJECT
    ${PROJECT_SOURCE_DIR}/src/render/bgfx/BgfxUIAtlasTexture.cpp
)
target_compile_features(tina_render_bgfx_ui_atlas_contract_test_object PRIVATE cxx_std_23)
target_compile_definitions(tina_render_bgfx_ui_atlas_contract_test_object PRIVATE
    bgfx=tina_test_bgfx
    createUISolidWhiteTexture=createUISolidWhiteTextureContractTest
    createUIGlyphAtlasTexture=createUIGlyphAtlasTextureContractTest
    updateUIGlyphAtlasTexture=updateUIGlyphAtlasTextureContractTest
)
target_include_directories(tina_render_bgfx_ui_atlas_contract_test_object BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes
    ${PROJECT_SOURCE_DIR}/src/render/bgfx
)
target_link_libraries(tina_render_bgfx_ui_atlas_contract_test_object PRIVATE
    Tina::ProjectOptions
    Tina::Core
)
set_target_properties(tina_render_bgfx_ui_atlas_contract_test_object PROPERTIES CXX_EXTENSIONS OFF)

add_executable(tina_render_bgfx_tests
    BgfxOpaque3DGeometryTests.cpp
    BgfxResourceSlotGenerationTests.cpp
    BgfxRetirementTimelineTests.cpp
    BgfxRenderDeviceFactoryTests.cpp
    BgfxSprite2DGeometryTests.cpp
    BgfxSurfaceFramePlannerTests.cpp
    BgfxTransientFrameBudgetTests.cpp
    BgfxUIAtlasTextureContractTests.cpp
    BgfxUIDisplayGeometryTests.cpp
    $<TARGET_OBJECTS:tina_render_bgfx_ui_atlas_contract_test_object>
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
