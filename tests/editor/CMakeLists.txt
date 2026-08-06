add_executable(tina_editor_tests
    World2DAuthoringDocumentTests.cpp
    World2DAuthoringFileTests.cpp
    header_isolation/EditorErrorsHeader.cpp
    header_isolation/World2DAuthoringDocumentHeader.cpp
    header_isolation/World2DAuthoringFileHeader.cpp
)
target_compile_features(tina_editor_tests PRIVATE cxx_std_23)
target_link_libraries(tina_editor_tests PRIVATE
    Tina::ProjectOptions
    Tina::Editor
    GTest::gtest_main
)
set_target_properties(tina_editor_tests PROPERTIES CXX_EXTENSIONS OFF)
