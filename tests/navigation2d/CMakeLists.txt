add_executable(tina_navigation2d_tests
    NavigationGrid2DTests.cpp
    NavigationPathfinder2DTests.cpp
    TileMapNavigation2DTests.cpp
    header_isolation/NavigationErrorsHeader.cpp
    header_isolation/NavigationGrid2DHeader.cpp
    header_isolation/NavigationPathfinder2DHeader.cpp
    header_isolation/TileMapNavigation2DHeader.cpp
)
target_compile_features(tina_navigation2d_tests PRIVATE cxx_std_23)
target_include_directories(tina_navigation2d_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(tina_navigation2d_tests PRIVATE
    Tina::ProjectOptions
    Tina::Navigation2D
    Tina::Asset
    GTest::gtest_main
)
set_target_properties(tina_navigation2d_tests PROPERTIES CXX_EXTENSIONS OFF)
