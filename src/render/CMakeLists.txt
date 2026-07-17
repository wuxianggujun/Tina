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
