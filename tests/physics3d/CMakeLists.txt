add_library(tina_physics3d_header_isolation OBJECT
    header_isolation/PhysicsErrorsHeader.cpp
    header_isolation/PhysicsIdsHeader.cpp
    header_isolation/PhysicsTypesHeader.cpp
    header_isolation/PhysicsWorld3DHeader.cpp
)
target_include_directories(tina_physics3d_header_isolation PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_physics3d_header_isolation PRIVATE Tina::ProjectOptions)
set_target_properties(tina_physics3d_header_isolation PROPERTIES CXX_EXTENSIONS OFF)

add_executable(tina_physics3d_tests
    PhysicsWorld3DTests.cpp
    FloatingOrigin3DTests.cpp
    $<TARGET_OBJECTS:tina_physics3d_header_isolation>
)
target_link_libraries(tina_physics3d_tests PRIVATE Tina::ProjectOptions Tina::Physics3D GTest::gtest_main)
set_target_properties(tina_physics3d_tests PROPERTIES CXX_EXTENSIONS OFF)
