set(TINA_ANIMATION3D_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/animation3d/AnimationErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/animation3d/AnimationGraph3D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/animation3d/BlendTree3D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/animation3d/ClipSampler3D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/animation3d/IkSolver3D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/animation3d/PoseBlend3D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/animation3d/Skeleton3D.hpp
)

add_library(tina_animation3d STATIC
    ${TINA_ANIMATION3D_PUBLIC_HEADERS}
    AnimationGraph3D.cpp
    BlendTree3D.cpp
    ClipSampler3D.cpp
    IkSolver3D.cpp
    PoseBlend3D.cpp
    Skeleton3D.cpp
)
add_library(Tina::Animation3D ALIAS tina_animation3d)

target_compile_features(tina_animation3d PUBLIC cxx_std_23)
target_include_directories(tina_animation3d
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# AssetFormat is PUBLIC because Skeleton3D::Create and ClipSampler3D::Create take cooked
# payload views in their signatures, and Scene is PUBLIC because a pose is a span of
# Scene::LocalTransform -- reusing that type rather than declaring a third TRS tuple, since
# a pose ends up in the Scene hierarchy for root motion anyway.
#
# Deliberately NOT linking Asset: this layer consumes payload views, never leases. Asset
# lifetime belongs to whoever owns the AssetSystem, which is the same boundary ADR 0031
# draws for Scene. Nor Render: the skinning palette is written into a caller-provided float
# span, so Render's item types never appear here.
target_link_libraries(tina_animation3d
    PUBLIC Tina::Core Tina::Math Tina::AssetFormat Tina::Scene Tina::Gameplay
    PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>"
)
set_target_properties(tina_animation3d PROPERTIES CXX_EXTENSIONS OFF)
