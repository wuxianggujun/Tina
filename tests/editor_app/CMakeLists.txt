add_executable(tina_editor_app_tests
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
    ${PROJECT_SOURCE_DIR}/src/editor_app
)
target_link_libraries(tina_editor_app_tests PRIVATE
    Tina::ProjectOptions
    Tina::EditorApp
    GTest::gtest_main
)
set_target_properties(tina_editor_app_tests PROPERTIES CXX_EXTENSIONS OFF)
