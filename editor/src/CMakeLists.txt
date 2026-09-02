add_library(tina_editor STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/EditorErrors.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/EditorDocumentTabs.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/EditorProjectCreation.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/EditorProjectWorkspace.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/EditorMarqueeSelection.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/EditorNodePropertyOperations.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/EditorPlaySession.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/EditorSceneOperations.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/EditorTransformGizmo.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/EditorViewportGrid.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/EditorViewportNavigation.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/EditorViewportPick.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/Fx2DAuthoringDocument.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/Navigation2DAuthoringDocument.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/ProjectAssetBrowser.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/SpriteAnimationAuthoringDocument.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/SpriteAnimationAuthoringFile.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/TileMapAuthoringDocument.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/TileMapAuthoringFile.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/TileMapGameplaySpawnPlan.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/World2DAuthoringDocument.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/World2DAuthoringFile.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/World3DAuthoringDocument.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tina/editor/World3DAuthoringFile.hpp
    World2DAuthoringDocument.cpp
    EditorNodePropertyOperations.cpp
    EditorDocumentTabs.cpp
    EditorMarqueeSelection.cpp
    EditorPlaySession.cpp
    EditorProjectCreation.cpp
    EditorProjectWorkspace.cpp
    EditorSceneOperations.cpp
    EditorTransformGizmo.cpp
    EditorViewportGrid.cpp
    EditorViewportNavigation.cpp
    EditorViewportPick.cpp
    Fx2DAuthoringDocument.cpp
    Navigation2DAuthoringDocument.cpp
    ProjectAssetBrowser.cpp
    SpriteAnimationAuthoringDocument.cpp
    SpriteAnimationAuthoringFile.cpp
    World2DAuthoringFile.cpp
    World3DAuthoringDocument.cpp
    World3DAuthoringFile.cpp
    TileMapAuthoringDocument.cpp
    TileMapAuthoringFile.cpp
    TileMapGameplaySpawnPlan.cpp
)
add_library(Tina::Editor ALIAS tina_editor)

target_compile_features(tina_editor PUBLIC cxx_std_23)
# Declared here rather than through tina_configure_game_sdk_target(): the editor left the SDK export,
# and that helper overwrites INTERFACE_INCLUDE_DIRECTORIES with the engine's include/ root.
target_include_directories(tina_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)
# Reaches src/core/io/PathUtil.hpp; see the note in src/asset/CMakeLists.txt. This is why the editor
# cannot build against an installed SDK alone -- PathUtil.hpp is private and never installed.
target_include_directories(tina_editor PRIVATE ${PROJECT_SOURCE_DIR}/src)
# Math is PUBLIC: EditorViewportPick.hpp names Math::Ray / Math::Sphere in its
# published candidate and query types.
target_link_libraries(tina_editor
    PUBLIC Tina::Core Tina::Math Tina::AssetFormat Tina::Asset
    PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>"
)
set_target_properties(tina_editor PROPERTIES CXX_EXTENSIONS OFF)
