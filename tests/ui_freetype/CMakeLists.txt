add_executable(tina_ui_freetype_tests
    FreeTypeTextRasterizerTests.cpp
    header_isolation/FreeTypeTextRasterizerFactoryHeader.cpp
)

target_compile_features(tina_ui_freetype_tests PRIVATE cxx_std_23)
tina_target_optional_ui_font(tina_ui_freetype_tests TINA_UI_FREETYPE_TEST_FONT_PATH)
target_link_libraries(tina_ui_freetype_tests
    PRIVATE
        Tina::ProjectOptions
        Tina::UIFreetype
        GTest::gtest_main
)
set_target_properties(tina_ui_freetype_tests PROPERTIES CXX_EXTENSIONS OFF)
