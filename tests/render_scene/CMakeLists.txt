add_executable(tina_render_scene_tests
    Camera2DPickTests.cpp
    Camera2DProjectionTests.cpp
    RenderPostProcessTests.cpp
    RenderSceneTests.cpp
    Sprite2DShadowMathTests.cpp
)

target_compile_features(tina_render_scene_tests PRIVATE cxx_std_23)
target_link_libraries(tina_render_scene_tests
    PRIVATE
        Tina::ProjectOptions
        Tina::Render
        GTest::gtest_main
)
target_include_directories(tina_render_scene_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/render)
set_target_properties(tina_render_scene_tests PROPERTIES CXX_EXTENSIONS OFF)
