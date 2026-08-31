set(TINA_SCENE_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/scene/Animator3D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/Camera2D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/CameraFollow2D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/DirectionalLight3D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/Entity.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/EntityMetadata.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/ExtractRenderScene.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/Fx2DFactory.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/MeshRenderer3D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/ParticleSystem2D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/PerspectiveCamera3D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/PhysicsBody2D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/PhysicsShape2D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/PointLight2D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/PointLight3D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/PrefabInstantiate.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/ResourceBinding2D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/Scene.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/SceneErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/ShadowOccluder2D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/SkinnedMeshRenderer3D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/SpotLight3D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/SpriteAnimationBinding2D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/SpriteAnimator2D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/SpriteRenderer2D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/Trail2D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/Transform.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/World.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/World2DSceneIndex.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/World2DSnapshot.hpp
)

add_library(tina_scene STATIC
    ${TINA_SCENE_PUBLIC_HEADERS}
    Animator3D.cpp
    CameraFollow2D.cpp
    ExtractRenderScene.cpp
    Fx2DFactory.cpp
    ParticleSystem2D.cpp
    PrefabInstantiate.cpp
    SpriteAnimator2D.cpp
    Trail2D.cpp
    World.cpp
    World2DSceneIndex.cpp
    World2DSnapshot.cpp
)
add_library(Tina::Scene ALIAS tina_scene)

target_compile_features(tina_scene PUBLIC cxx_std_23)
target_include_directories(tina_scene PUBLIC ${PROJECT_SOURCE_DIR}/include)
# Math is PUBLIC here, unlike in Render: Transform.hpp and SpriteRenderer2D.hpp
# name Math::Vec3 / Math::Vec2 / Math::Quaternion in published component fields.
target_link_libraries(tina_scene
    PUBLIC Tina::Core Tina::Math Tina::Render Tina::AssetFormat Tina::AssetTypes
    PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>"
)
set_target_properties(tina_scene PROPERTIES CXX_EXTENSIONS OFF)
