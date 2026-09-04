set(TINA_GAMEPLAY2D_HEADER_ISOLATION_SOURCES
    header_isolation/Scene2DRuntimeHeader.cpp
)

add_library(tina_gameplay2d_header_isolation OBJECT
    ${TINA_GAMEPLAY2D_HEADER_ISOLATION_SOURCES}
)
target_compile_features(tina_gameplay2d_header_isolation PRIVATE cxx_std_23)
target_include_directories(tina_gameplay2d_header_isolation PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(tina_gameplay2d_header_isolation PRIVATE
    Tina::ProjectOptions
)
set_target_properties(tina_gameplay2d_header_isolation PROPERTIES CXX_EXTENSIONS OFF)

# Isolation compiles Scene2DRuntime.hpp without TINA_HAS_PHYSICS2D, so a
# physics-off consumer cannot silently depend on Physics2D types leaking
# through the public header. The physics-on surface is covered by
# tests/physics2d/header_isolation/Scene2DRuntimeHeader.cpp.
#
# Deliberately NOT linked into the executable via $<TARGET_OBJECTS:...>, unlike
# every other isolation target here. Tina::Gameplay2D carries TINA_HAS_PHYSICS2D
# as a PUBLIC define, so in a physics-on build the executable's translation units
# see the physics members of Scene2DRuntime while this object was compiled without
# them -- two layouts for one class in one image. Depending on it instead keeps the
# compile as the assertion without linking the mismatched object.
add_executable(tina_gameplay2d_tests
    Gameplay2DIsolationTests.cpp
)
add_dependencies(tina_gameplay2d_tests tina_gameplay2d_header_isolation)
target_compile_features(tina_gameplay2d_tests PRIVATE cxx_std_23)
target_link_libraries(tina_gameplay2d_tests PRIVATE
    Tina::ProjectOptions
    Tina::Gameplay2D
    GTest::gtest_main
)
set_target_properties(tina_gameplay2d_tests PROPERTIES CXX_EXTENSIONS OFF)
