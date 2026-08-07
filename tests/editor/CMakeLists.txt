add_executable(tina_editor_tests
    EditorDocumentTabsTests.cpp
    ProjectAssetBrowserTests.cpp
    SpriteAnimationAuthoringDocumentTests.cpp
    TileMapAuthoringDocumentTests.cpp
    World2DAuthoringDocumentTests.cpp
    World2DAuthoringFileTests.cpp
    World3DAuthoringDocumentTests.cpp
    World3DAuthoringFileTests.cpp
    header_isolation/EditorErrorsHeader.cpp
    header_isolation/EditorDocumentTabsHeader.cpp
    header_isolation/ProjectAssetBrowserHeader.cpp
    header_isolation/SpriteAnimationAuthoringDocumentHeader.cpp
    header_isolation/TileMapAuthoringDocumentHeader.cpp
    header_isolation/World2DAuthoringDocumentHeader.cpp
    header_isolation/World2DAuthoringFileHeader.cpp
    header_isolation/World3DAuthoringDocumentHeader.cpp
    header_isolation/World3DAuthoringFileHeader.cpp
)
target_compile_features(tina_editor_tests PRIVATE cxx_std_23)
target_link_libraries(tina_editor_tests PRIVATE
    Tina::ProjectOptions
    Tina::Editor
    GTest::gtest_main
)
set_target_properties(tina_editor_tests PROPERTIES CXX_EXTENSIONS OFF)
