add_library(tina_render STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/render/RenderDevice.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/RenderErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/RenderFrame.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/RenderSurface.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/render/null/NullRenderDeviceFactory.hpp
    RenderSurfaceStateTracker.cpp
    RenderSurfaceStateTracker.hpp
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
        bgfx/BgfxSurfaceFramePlanner.cpp
        bgfx/BgfxSurfaceFramePlanner.hpp
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
    )
    set_target_properties(tina_render_bgfx PROPERTIES
        CXX_EXTENSIONS OFF
        EXPORT_NAME RenderBgfx
    )
endif()
