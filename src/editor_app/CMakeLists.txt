add_library(tina_editor_app STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/editor_app/EditorApplication.hpp
    EditorApplication.cpp
)
add_library(Tina::EditorApp ALIAS tina_editor_app)

target_compile_features(tina_editor_app PUBLIC cxx_std_23)
target_include_directories(tina_editor_app
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
)
target_link_libraries(tina_editor_app
    PRIVATE
        Tina::DesktopBootstrap
        Tina::Asset
        Tina::Editor
        Tina::Runtime
        Tina::Scene
        "$<BUILD_INTERFACE:Tina::ProjectOptions>"
)
set_target_properties(tina_editor_app PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME EditorApp
)
