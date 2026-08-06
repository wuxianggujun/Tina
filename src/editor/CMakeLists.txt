add_library(tina_editor STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/editor/EditorErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/editor/World2DAuthoringDocument.hpp
    World2DAuthoringDocument.cpp
)
add_library(Tina::Editor ALIAS tina_editor)

target_compile_features(tina_editor PUBLIC cxx_std_23)
target_include_directories(tina_editor PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_editor
    PUBLIC Tina::Core Tina::AssetFormat
    PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>"
)
set_target_properties(tina_editor PROPERTIES CXX_EXTENSIONS OFF)
