add_library(tina_window_surface_integration STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/integration/WindowSurface.hpp
    WindowSurfaceLease.cpp
    WindowSurfaceLeaseAccess.hpp
)
add_library(Tina::WindowSurfaceIntegration ALIAS tina_window_surface_integration)

target_compile_features(tina_window_surface_integration PUBLIC cxx_std_23)
target_include_directories(tina_window_surface_integration
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(tina_window_surface_integration
    PUBLIC
        Tina::Core
        Tina::Platform
    PRIVATE
        Tina::ProjectOptions
)
set_target_properties(tina_window_surface_integration PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME WindowSurfaceIntegration
)
