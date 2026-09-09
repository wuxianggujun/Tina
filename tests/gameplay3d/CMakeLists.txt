add_library(tina_gameplay3d_header_isolation OBJECT
    header_isolation/Scene3DRuntimeHeader.cpp
)
target_compile_features(tina_gameplay3d_header_isolation PRIVATE cxx_std_23)
target_include_directories(tina_gameplay3d_header_isolation PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(tina_gameplay3d_header_isolation PRIVATE
    Tina::ProjectOptions
)
set_target_properties(tina_gameplay3d_header_isolation PROPERTIES CXX_EXTENSIONS OFF)

# Keep the header-only compile independent from the PUBLIC physics define carried
# by Tina::GameSDK; otherwise one executable could contain two Scene3DRuntime
# layouts when physics is enabled.
add_executable(tina_gameplay3d_tests
    Gameplay3DIsolationTests.cpp
)
add_dependencies(tina_gameplay3d_tests tina_gameplay3d_header_isolation)
target_compile_features(tina_gameplay3d_tests PRIVATE cxx_std_23)
target_link_libraries(tina_gameplay3d_tests PRIVATE
    Tina::ProjectOptions
    Tina::GameSDK
    GTest::gtest_main
)
set_target_properties(tina_gameplay3d_tests PROPERTIES CXX_EXTENSIONS OFF)
