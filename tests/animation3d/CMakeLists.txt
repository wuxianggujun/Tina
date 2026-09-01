set(TINA_ANIMATION3D_HEADER_ISOLATION_SOURCES
    header_isolation/AnimationErrorsHeader.cpp
    header_isolation/AnimationGraph3DHeader.cpp
    header_isolation/BlendTree3DHeader.cpp
    header_isolation/ClipSampler3DHeader.cpp
    header_isolation/IkSolver3DHeader.cpp
    header_isolation/PoseBlend3DHeader.cpp
    header_isolation/Skeleton3DHeader.cpp
)

# One TU per public header, compiled with nothing else included, so a header relying on
# another's include order fails here rather than in the first consumer that includes them
# in a different order.
add_library(tina_animation3d_header_isolation OBJECT
    ${TINA_ANIMATION3D_HEADER_ISOLATION_SOURCES}
)
target_compile_features(tina_animation3d_header_isolation PRIVATE cxx_std_23)
target_include_directories(tina_animation3d_header_isolation PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(tina_animation3d_header_isolation PRIVATE
    Tina::ProjectOptions
)
set_target_properties(tina_animation3d_header_isolation PROPERTIES CXX_EXTENSIONS OFF)

add_executable(tina_animation3d_tests
    AnimationGraph3DTests.cpp
    ClipSampler3DTests.cpp
    IkSolver3DTests.cpp
    PoseBlend3DTests.cpp
    Skeleton3DTests.cpp
    $<TARGET_OBJECTS:tina_animation3d_header_isolation>
)
target_compile_features(tina_animation3d_tests PRIVATE cxx_std_23)
target_link_libraries(tina_animation3d_tests PRIVATE
    Tina::ProjectOptions
    Tina::Animation3D
    GTest::gtest_main
)
set_target_properties(tina_animation3d_tests PROPERTIES CXX_EXTENSIONS OFF)
