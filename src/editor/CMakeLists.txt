add_library(tina_editor STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/editor/EditorErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/editor/EditorDocumentTabs.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/editor/EditorProjectCreation.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/editor/EditorProjectWorkspace.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/editor/ProjectAssetBrowser.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/editor/SpriteAnimationAuthoringDocument.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/editor/SpriteAnimationAuthoringFile.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/editor/TileMapAuthoringDocument.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/editor/TileMapAuthoringFile.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/editor/World2DAuthoringDocument.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/editor/World2DAuthoringFile.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/editor/World3DAuthoringDocument.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/editor/World3DAuthoringFile.hpp
    World2DAuthoringDocument.cpp
    EditorDocumentTabs.cpp
    EditorProjectCreation.cpp
    EditorProjectWorkspace.cpp
    ProjectAssetBrowser.cpp
    SpriteAnimationAuthoringDocument.cpp
    SpriteAnimationAuthoringFile.cpp
    World2DAuthoringFile.cpp
    World3DAuthoringDocument.cpp
    World3DAuthoringFile.cpp
    TileMapAuthoringDocument.cpp
    TileMapAuthoringFile.cpp
)
add_library(Tina::Editor ALIAS tina_editor)

target_compile_features(tina_editor PUBLIC cxx_std_23)
target_include_directories(tina_editor PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_editor
    PUBLIC Tina::Core Tina::AssetFormat
    PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>"
)
set_target_properties(tina_editor PROPERTIES CXX_EXTENSIONS OFF)
