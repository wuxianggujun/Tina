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

add_library(tina_render_bgfx_csm_contract_test_object OBJECT
    ${PROJECT_SOURCE_DIR}/src/render/bgfx/BgfxCascadedDirectionalShadowResources.cpp
)
target_compile_features(tina_render_bgfx_csm_contract_test_object PRIVATE cxx_std_23)
target_compile_definitions(tina_render_bgfx_csm_contract_test_object PRIVATE
    bgfx=tina_test_bgfx
    BgfxCascadedDirectionalShadowResources=BgfxCascadedDirectionalShadowResourcesContractTest
    createCascadedDirectionalShadowResources=createCascadedDirectionalShadowResourcesContractTest
    destroyCascadedDirectionalShadowResources=destroyCascadedDirectionalShadowResourcesContractTest
)
target_include_directories(tina_render_bgfx_csm_contract_test_object BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes
    ${PROJECT_SOURCE_DIR}/src/render/bgfx
)
target_link_libraries(tina_render_bgfx_csm_contract_test_object PRIVATE
    Tina::ProjectOptions
    Tina::Core
)
set_target_properties(tina_render_bgfx_csm_contract_test_object PROPERTIES
    CXX_EXTENSIONS OFF
)

add_library(tina_render_bgfx_spot_contract_obj OBJECT
    ${PROJECT_SOURCE_DIR}/src/render/bgfx/BgfxSpotLightShadowResources.cpp
)
target_compile_features(tina_render_bgfx_spot_contract_obj PRIVATE cxx_std_23)
target_compile_definitions(tina_render_bgfx_spot_contract_obj PRIVATE
    bgfx=tina_test_bgfx
    BgfxSpotLightShadowResources=BgfxSpotLightShadowResourcesContractTest
    createSpotLightShadowResources=createSpotLightShadowResourcesContractTest
    destroySpotLightShadowResources=destroySpotLightShadowResourcesContractTest
)
target_include_directories(tina_render_bgfx_spot_contract_obj BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes
    ${PROJECT_SOURCE_DIR}/src/render/bgfx
)
target_link_libraries(tina_render_bgfx_spot_contract_obj PRIVATE
    Tina::ProjectOptions
    Tina::Core
)
set_target_properties(tina_render_bgfx_spot_contract_obj PROPERTIES
    CXX_EXTENSIONS OFF
)

add_library(tina_render_bgfx_point_contract_obj OBJECT
    ${PROJECT_SOURCE_DIR}/src/render/bgfx/BgfxPointLightShadowResources.cpp
)
target_compile_features(tina_render_bgfx_point_contract_obj PRIVATE cxx_std_23)
target_compile_definitions(tina_render_bgfx_point_contract_obj PRIVATE
    bgfx=tina_test_bgfx
    BgfxPointLightShadowResources=BgfxPointLightShadowResourcesContractTest
    createPointLightShadowResources=createPointLightShadowResourcesContractTest
    destroyPointLightShadowResources=destroyPointLightShadowResourcesContractTest
)
target_include_directories(tina_render_bgfx_point_contract_obj BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes
    ${PROJECT_SOURCE_DIR}/src/render/bgfx
)
target_link_libraries(tina_render_bgfx_point_contract_obj PRIVATE
    Tina::ProjectOptions
    Tina::Render
)
set_target_properties(tina_render_bgfx_point_contract_obj PROPERTIES
    CXX_EXTENSIONS OFF
)

add_library(tina_render_bgfx_environment_map_contract_test_object OBJECT
    ${PROJECT_SOURCE_DIR}/src/render/bgfx/BgfxEnvironmentMapResources.cpp
)
target_compile_features(tina_render_bgfx_environment_map_contract_test_object PRIVATE cxx_std_23)
target_compile_definitions(tina_render_bgfx_environment_map_contract_test_object PRIVATE
    bgfx=tina_test_bgfx
    BgfxEnvironmentMapResources=BgfxEnvironmentMapResourcesContractTest
    createEnvironmentMapResources=createEnvironmentMapResourcesContractTest
    destroyEnvironmentMapResources=destroyEnvironmentMapResourcesContractTest
)
target_include_directories(tina_render_bgfx_environment_map_contract_test_object BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes
    ${PROJECT_SOURCE_DIR}/src/render/bgfx
)
target_link_libraries(tina_render_bgfx_environment_map_contract_test_object PRIVATE
    Tina::ProjectOptions
    Tina::Render
)
set_target_properties(tina_render_bgfx_environment_map_contract_test_object PROPERTIES
    CXX_EXTENSIONS OFF
)

add_library(tina_render_bgfx_texture2d_upload_contract_obj OBJECT
    ${PROJECT_SOURCE_DIR}/src/render/bgfx/BgfxTexture2DUpload.cpp
)
target_compile_features(tina_render_bgfx_texture2d_upload_contract_obj PRIVATE cxx_std_23)
target_compile_definitions(tina_render_bgfx_texture2d_upload_contract_obj PRIVATE
    bgfx=tina_test_bgfx
    toBgfxTextureFormat=toBgfxTextureFormatContractTest
    toBgfxTexture2DFlags=toBgfxTexture2DFlagsContractTest
    createTexture2DUpload=createTexture2DUploadContractTest
)
target_include_directories(tina_render_bgfx_texture2d_upload_contract_obj BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes
    ${PROJECT_SOURCE_DIR}/src/render/bgfx
)
target_link_libraries(tina_render_bgfx_texture2d_upload_contract_obj PRIVATE
    Tina::ProjectOptions
    Tina::Render
)
set_target_properties(tina_render_bgfx_texture2d_upload_contract_obj PROPERTIES
    CXX_EXTENSIONS OFF
)

add_executable(tina_render_bgfx_tests
    BgfxOpaque3DGeometryTests.cpp
    BgfxCascadedDirectionalShadowMathTests.cpp
    BgfxCascadedDirectionalShadowResourcesContractTests.cpp
    BgfxSpotLightShadowMathTests.cpp
    BgfxSpotLightShadowResourcesContractTests.cpp
    BgfxPointLightShadowMathTests.cpp
    BgfxPointLightShadowResourcesContractTests.cpp
    BgfxEnvironmentMapResourcesContractTests.cpp
    BgfxResourceSlotGenerationTests.cpp
    BgfxRetirementTimelineTests.cpp
    BgfxRenderDeviceFactoryTests.cpp
    BgfxSprite2DGeometryTests.cpp
    BgfxSurfaceFramePlannerTests.cpp
    BgfxTexture2DUploadContractTests.cpp
    BgfxTransientFrameBudgetTests.cpp
    BgfxUIAtlasTextureContractTests.cpp
    BgfxUIDisplayGeometryTests.cpp
    $<TARGET_OBJECTS:tina_render_bgfx_ui_atlas_contract_test_object>
    $<TARGET_OBJECTS:tina_render_bgfx_csm_contract_test_object>
    $<TARGET_OBJECTS:tina_render_bgfx_spot_contract_obj>
    $<TARGET_OBJECTS:tina_render_bgfx_point_contract_obj>
    $<TARGET_OBJECTS:tina_render_bgfx_environment_map_contract_test_object>
    $<TARGET_OBJECTS:tina_render_bgfx_texture2d_upload_contract_obj>
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
