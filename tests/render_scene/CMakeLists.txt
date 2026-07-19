add_executable(tina_render_scene_tests
    RenderSceneTests.cpp
)

target_compile_features(tina_render_scene_tests PRIVATE cxx_std_23)
target_link_libraries(tina_render_scene_tests
    PRIVATE
        Tina::ProjectOptions
        Tina::Render
        GTest::gtest_main
)
set_target_properties(tina_render_scene_tests PROPERTIES CXX_EXTENSIONS OFF)
