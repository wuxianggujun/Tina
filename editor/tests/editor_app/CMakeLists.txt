add_executable(tina_editor_app_tests
    EditorCompositeImageResolverTests.cpp
    EditorSourceImportLaunchOptionsTests.cpp
    EditorSourceImportIngressTests.cpp
    EditorSourceImportSelectionTests.cpp
    EditorSourceImportServiceTests.cpp
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(tina_editor_app_tests PRIVATE
        EditorFileDialogLinuxTests.cpp
    )
endif()
target_compile_features(tina_editor_app_tests PRIVATE cxx_std_23)
target_include_directories(tina_editor_app_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/editor/app
)
# These tests compile tina_editor_app's private headers, which include engine public headers. The
# library reaches those through PRIVATE links, so a consumer of Tina::EditorApp does not inherit
# them -- the test has to name the same set itself. Before the editor moved out of src/ this
# resolved by accident: Tina::EditorApp's PUBLIC include directory was the engine's whole include/
# root, so every tina/... header was reachable whether or not it was part of the contract.
target_link_libraries(tina_editor_app_tests PRIVATE
    Tina::ProjectOptions
    Tina::EditorApp
    Tina::Asset
    Tina::Scene
    Tina::UI
    Tina::DesktopBootstrap
    Tina::Editor
    GTest::gtest_main
)
set_target_properties(tina_editor_app_tests PROPERTIES CXX_EXTENSIONS OFF)
