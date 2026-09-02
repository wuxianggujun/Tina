add_library(tina_render STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/render/Camera2DPick.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/Camera2DProjection.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/RenderDevice.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/RenderErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/FramePin.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/FrameResource.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/Texture2DFrameResourceResolver.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/RenderFrame.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/RenderFrameCapture.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/RenderFramePacket.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/RenderPassScheduler.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/RenderPostProcess.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/RenderScene.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/RenderSurface.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/ShadowMapExtentConfig.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/UIDisplayList.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/UploadTicket.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/WorldPointerSample.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/null/NullRenderDeviceFactory.hpp
    Camera2DPick.cpp
    Camera2DProjection.cpp
    EnvironmentMapUpload.cpp
    Mesh3DLighting.cpp
    RenderPassScheduler.cpp
    RenderPostProcess.cpp
    RenderSurfaceStateTracker.cpp
    RenderSurfaceStateTracker.hpp
    RenderScene.cpp
    Sprite2DLighting.cpp
    Sprite2DShadowMath.hpp
    ShadowMapExtentConfig.cpp
    Texture2DUpload.cpp
    UIDisplayList.cpp
    UploadTicket.cpp
    VideoDecode.cpp
    null/NullRenderDevice.cpp
)
add_library(Tina::Render ALIAS tina_render)

target_compile_features(tina_render PUBLIC cxx_std_23)
target_include_directories(tina_render PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_render PUBLIC Tina::Core)
# Math is PRIVATE because Render's public headers still publish flat float fields;
# it appears only inside RenderScene.cpp. Making it PUBLIC would advertise a
# dependency the published contract does not actually have.
target_link_libraries(tina_render PRIVATE
    "$<BUILD_INTERFACE:Tina::ProjectOptions>"
    Tina::Math
)
set_target_properties(tina_render PROPERTIES CXX_EXTENSIONS OFF)

if(TINA_BUILD_RENDER_BGFX)
    add_library(tina_render_bgfx STATIC
        bgfx/BgfxRenderDevice.cpp
        bgfx/BgfxRenderDevice.hpp
        bgfx/BgfxEnvironmentMapResources.cpp
        bgfx/BgfxEnvironmentMapResources.hpp
        bgfx/BgfxOpaque3DGeometry.cpp
        bgfx/BgfxOpaque3DGeometry.hpp
        bgfx/BgfxCascadedDirectionalShadowMath.cpp
        bgfx/BgfxCascadedDirectionalShadowMath.hpp
        bgfx/BgfxCascadedDirectionalShadowResources.cpp
        bgfx/BgfxCascadedDirectionalShadowResources.hpp
        bgfx/BgfxSpotLightShadowMath.cpp
        bgfx/BgfxSpotLightShadowMath.hpp
        bgfx/BgfxSpotLightShadowResources.cpp
        bgfx/BgfxSpotLightShadowResources.hpp
        bgfx/BgfxPointLightShadowMath.cpp
        bgfx/BgfxPointLightShadowMath.hpp
        bgfx/BgfxPointLightShadowResources.cpp
        bgfx/BgfxPointLightShadowResources.hpp
        bgfx/BgfxOpaque3DShader.cpp
        bgfx/BgfxOpaque3DShader.hpp
        bgfx/BgfxResourceSlotGeneration.hpp
        bgfx/BgfxSprite2DGeometry.cpp
        bgfx/BgfxSprite2DGeometry.hpp
        bgfx/BgfxSprite2DShader.cpp
        bgfx/BgfxSprite2DShader.hpp
        bgfx/BgfxSurfaceFramePlanner.cpp
        bgfx/BgfxSurfaceFramePlanner.hpp
        bgfx/BgfxTransientFrameBudget.cpp
        bgfx/BgfxTransientFrameBudget.hpp
        bgfx/BgfxTexture2DUpload.cpp
        bgfx/BgfxTexture2DUpload.hpp
        bgfx/BgfxVideoDecode.cpp
        bgfx/BgfxVideoDecode.hpp
        bgfx/BgfxUIAtlasTexture.cpp
        bgfx/BgfxUIAtlasTexture.hpp
        bgfx/BgfxUIDisplayGeometry.cpp
        bgfx/BgfxUIDisplayGeometry.hpp
        bgfx/BgfxUIImageShader.cpp
        bgfx/BgfxUIImageShader.hpp
        bgfx/BgfxUITexturedShader.cpp
        bgfx/BgfxUITexturedShader.hpp
    )
    add_library(Tina::RenderBgfx ALIAS tina_render_bgfx)

    target_compile_features(tina_render_bgfx PUBLIC cxx_std_23)
    target_include_directories(tina_render_bgfx PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src/integration
    )
    target_link_libraries(tina_render_bgfx
        PUBLIC
            Tina::Render
        PRIVATE
            "$<BUILD_INTERFACE:Tina::ProjectOptions>"
            Tina::WindowSurfaceIntegration
            TinaBgfxRuntime::bgfx
            TinaBgfxRuntime::bx
    )
    set_target_properties(tina_render_bgfx PROPERTIES
        CXX_EXTENSIONS OFF
        EXPORT_NAME RenderBgfx
    )
    include(TinaBgfxEmbeddedShaders)
    if(TINA_RENDER_BGFX_MOBILE_SHADERS)
        # The shader tables include the essl headers behind this macro. Keeping the
        # option and the macro in one place means a table can never reference a header
        # the cook step did not produce.
        target_compile_definitions(tina_render_bgfx PRIVATE TINA_RENDER_BGFX_MOBILE_SHADERS=1)
    endif()
    tina_add_bgfx_embedded_shader(
        tina_render_bgfx
        tina_ui_textured_quad
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/vs_tina_ui_textured_quad.sc"
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/fs_tina_ui_textured_quad.sc"
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/tina_ui_textured_quad.def.sc"
    )
    tina_add_bgfx_embedded_shader(
        tina_render_bgfx
        tina_ui_image_quad
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/vs_tina_ui_image_quad.sc"
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/fs_tina_ui_image_quad.sc"
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/tina_ui_image_quad.def.sc"
    )
    tina_add_bgfx_embedded_shader(
        tina_render_bgfx
        tina_opaque3d_mr
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/vs_tina_opaque3d_mr.sc"
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/fs_tina_opaque3d_mr.sc"
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/tina_opaque3d_mr.def.sc"
    )
    tina_add_bgfx_embedded_vertex_shader(
        tina_render_bgfx
        tina_opaque3d_skinned
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/vs_tina_opaque3d_skinned.sc"
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/tina_opaque3d_skinned.def.sc"
    )
    tina_add_bgfx_embedded_shader(
        tina_render_bgfx
        tina_opaque3d_csm_depth
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/vs_tina_opaque3d_csm_depth.sc"
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/fs_tina_opaque3d_csm_depth.sc"
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/tina_opaque3d_csm_depth.def.sc"
    )
    tina_add_bgfx_embedded_shader(
        tina_render_bgfx
        tina_sprite2d_fixture
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/vs_tina_sprite2d_fixture.sc"
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/fs_tina_sprite2d_fixture.sc"
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/tina_sprite2d_fixture.def.sc"
    )
endif()
