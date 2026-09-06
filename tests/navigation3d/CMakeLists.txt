add_executable(tina_navigation3d_tests
    NavigationVolume3DTests.cpp
    NavigationPathfinder3DTests.cpp
    header_isolation/NavigationErrors3DHeader.cpp
    header_isolation/NavigationVolume3DHeader.cpp
    header_isolation/NavigationPathfinder3DHeader.cpp
)
target_compile_features(tina_navigation3d_tests PRIVATE cxx_std_23)
target_include_directories(tina_navigation3d_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(tina_navigation3d_tests PRIVATE
    Tina::ProjectOptions
    Tina::Navigation3D
    GTest::gtest_main
)
set_target_properties(tina_navigation3d_tests PROPERTIES CXX_EXTENSIONS OFF)
