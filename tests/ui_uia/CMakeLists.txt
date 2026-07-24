add_executable(tina_ui_uia_tests
    WindowsUiaAccessibilityTests.cpp
    header_isolation/WindowsUiaAccessibilityProviderFactoryHeader.cpp
)

target_compile_features(tina_ui_uia_tests PRIVATE cxx_std_23)
target_include_directories(tina_ui_uia_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/ui/uia
)
target_link_libraries(tina_ui_uia_tests
    PRIVATE
        Tina::ProjectOptions
        Tina::UI
        Tina::UIUia
        GTest::gtest_main
)
set_target_properties(tina_ui_uia_tests PROPERTIES CXX_EXTENSIONS OFF)
