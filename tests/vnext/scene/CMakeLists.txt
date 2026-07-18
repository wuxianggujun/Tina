set(TINA_SCENE_TEST_SOURCES
    SceneWorldTests.cpp
    header_isolation/EntityHeader.cpp
    header_isolation/SceneErrorsHeader.cpp
    header_isolation/SceneHeader.cpp
    header_isolation/TransformHeader.cpp
    header_isolation/WorldHeader.cpp
)

add_executable(tina_scene_tests ${TINA_SCENE_TEST_SOURCES})
target_compile_features(tina_scene_tests PRIVATE cxx_std_23)
target_include_directories(tina_scene_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(tina_scene_tests PRIVATE
    Tina::ProjectOptions
    Tina::Scene
    GTest::gtest_main
)
set_target_properties(tina_scene_tests PROPERTIES CXX_EXTENSIONS OFF)
