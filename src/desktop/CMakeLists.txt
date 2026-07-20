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
if(TINA_BUILD_UI_FREETYPE)
    target_compile_definitions(tina_bootstrap_desktop
        PRIVATE
            TINA_HAS_UI_FREETYPE=1
            TINA_DESKTOP_UI_FONT_PATH="${PROJECT_SOURCE_DIR}/resources/fonts/SourceHanSansSC-Regular.otf"
    )
    target_link_libraries(tina_bootstrap_desktop PRIVATE Tina::UIFreetype)
endif()
set_target_properties(tina_bootstrap_desktop PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME DesktopBootstrap
)
