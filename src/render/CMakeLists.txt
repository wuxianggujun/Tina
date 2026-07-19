add_library(tina_render STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/render/RenderDevice.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/RenderErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/RenderFrame.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/RenderScene.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/RenderSurface.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/UIDisplayList.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/null/NullRenderDeviceFactory.hpp
    RenderSurfaceStateTracker.cpp
    RenderSurfaceStateTracker.hpp
    RenderScene.cpp
    UIDisplayList.cpp
    null/NullRenderDevice.cpp
)
add_library(Tina::Render ALIAS tina_render)

target_compile_features(tina_render PUBLIC cxx_std_23)
target_include_directories(tina_render PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_render PUBLIC Tina::Core)
target_link_libraries(tina_render PRIVATE Tina::ProjectOptions)
set_target_properties(tina_render PROPERTIES CXX_EXTENSIONS OFF)

if(TINA_BUILD_RENDER_BGFX)
    add_library(tina_render_bgfx STATIC
        bgfx/BgfxRenderDevice.cpp
        bgfx/BgfxRenderDevice.hpp
        bgfx/BgfxOpaque3DGeometry.cpp
        bgfx/BgfxOpaque3DGeometry.hpp
        bgfx/BgfxOpaque3DShader.cpp
        bgfx/BgfxOpaque3DShader.hpp
        bgfx/BgfxSurfaceFramePlanner.cpp
        bgfx/BgfxSurfaceFramePlanner.hpp
        bgfx/BgfxTransientFrameBudget.cpp
        bgfx/BgfxTransientFrameBudget.hpp
        bgfx/BgfxUIDisplayGeometry.cpp
        bgfx/BgfxUIDisplayGeometry.hpp
        bgfx/BgfxUISolidQuadShader.cpp
        bgfx/BgfxUISolidQuadShader.hpp
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
            Tina::ProjectOptions
            Tina::WindowSurfaceIntegration
            bgfx
            bx
    )
    set_target_properties(tina_render_bgfx PROPERTIES
        CXX_EXTENSIONS OFF
        EXPORT_NAME RenderBgfx
    )

    include(TinaBgfxEmbeddedShaders)
    tina_add_bgfx_embedded_shader(
        tina_render_bgfx
        tina_ui_solid_quad
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/vs_tina_ui_solid_quad.sc"
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/fs_tina_ui_solid_quad.sc"
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/tina_ui_solid_quad.def.sc"
    )
    tina_add_bgfx_embedded_shader(
        tina_render_bgfx
        tina_opaque3d_unlit
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/vs_tina_opaque3d_unlit.sc"
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/fs_tina_opaque3d_unlit.sc"
        "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders/tina_opaque3d_unlit.def.sc"
    )
endif()
