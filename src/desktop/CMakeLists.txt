add_library(tina_bootstrap_desktop STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/desktop/DesktopEngine.hpp
    DesktopEngine.cpp
)
add_library(Tina::DesktopBootstrap ALIAS tina_bootstrap_desktop)

target_compile_features(tina_bootstrap_desktop PUBLIC cxx_std_23)
target_include_directories(tina_bootstrap_desktop
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(tina_bootstrap_desktop
    PUBLIC
        Tina::Runtime
    PRIVATE
        Tina::PlatformGlfw
        Tina::RenderBgfx
        Tina::Task
        Tina::ProjectOptions
)
set_target_properties(tina_bootstrap_desktop PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME DesktopBootstrap
)
