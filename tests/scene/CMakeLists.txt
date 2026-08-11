set(TINA_SCENE_TEST_SOURCES
    CameraFollow2DTests.cpp
    SceneWorldTests.cpp
    SceneComponentExtractTests.cpp
    ParticleSystem2DTests.cpp
    SpriteAnimator2DTests.cpp
    Trail2DTests.cpp
    World2DSnapshotTests.cpp
    header_isolation/Camera2DHeader.cpp
    header_isolation/CameraFollow2DHeader.cpp
    header_isolation/DirectionalLight3DHeader.cpp
    header_isolation/EntityHeader.cpp
    header_isolation/ExtractRenderSceneHeader.cpp
    header_isolation/MeshRenderer3DHeader.cpp
    header_isolation/ParticleSystem2DHeader.cpp
    header_isolation/PerspectiveCamera3DHeader.cpp
    header_isolation/PointLight2DHeader.cpp
    header_isolation/PointLight3DHeader.cpp
    header_isolation/PrefabInstantiateHeader.cpp
    header_isolation/SceneErrorsHeader.cpp
    header_isolation/SceneHeader.cpp
    header_isolation/ShadowOccluder2DHeader.cpp
    header_isolation/SpotLight3DHeader.cpp
    header_isolation/SpriteAnimator2DHeader.cpp
    header_isolation/SpriteRenderer2DHeader.cpp
    header_isolation/Trail2DHeader.cpp
    header_isolation/TransformHeader.cpp
    header_isolation/WorldHeader.cpp
    header_isolation/World2DSnapshotHeader.cpp
)

add_executable(tina_scene_tests ${TINA_SCENE_TEST_SOURCES})
target_compile_features(tina_scene_tests PRIVATE cxx_std_23)
target_include_directories(tina_scene_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(tina_scene_tests PRIVATE
    Tina::ProjectOptions
    Tina::Asset
    Tina::Scene
    Tina::Render
    GTest::gtest_main
)
set_target_properties(tina_scene_tests PROPERTIES CXX_EXTENSIONS OFF)
