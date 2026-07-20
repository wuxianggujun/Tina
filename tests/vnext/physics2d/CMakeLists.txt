set(TINA_PHYSICS2D_HEADER_ISOLATION_SOURCES
    header_isolation/PhysicsErrorsHeader.cpp
    header_isolation/PhysicsIdsHeader.cpp
    header_isolation/PhysicsTypesHeader.cpp
    header_isolation/PhysicsWorld2DHeader.cpp
)

add_library(tina_physics2d_header_isolation OBJECT
    ${TINA_PHYSICS2D_HEADER_ISOLATION_SOURCES}
)
target_compile_features(tina_physics2d_header_isolation PRIVATE cxx_std_23)
target_include_directories(tina_physics2d_header_isolation PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(tina_physics2d_header_isolation PRIVATE
    Tina::ProjectOptions
)
set_target_properties(tina_physics2d_header_isolation PROPERTIES CXX_EXTENSIONS OFF)

add_executable(tina_physics2d_tests
    PhysicsWorld2DTests.cpp
    $<TARGET_OBJECTS:tina_physics2d_header_isolation>
)
target_compile_features(tina_physics2d_tests PRIVATE cxx_std_23)
target_include_directories(tina_physics2d_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/tests
)
target_link_libraries(tina_physics2d_tests PRIVATE
    Tina::ProjectOptions
    Tina::Physics2D
    GTest::gtest_main
)
set_target_properties(tina_physics2d_tests PROPERTIES CXX_EXTENSIONS OFF)
