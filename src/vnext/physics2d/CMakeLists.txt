set(TINA_PHYSICS2D_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/physics2d/PhysicsErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/physics2d/PhysicsIds.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/physics2d/PhysicsTypes.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/physics2d/PhysicsWorld2D.hpp
)

add_library(tina_physics2d STATIC
    ${TINA_PHYSICS2D_PUBLIC_HEADERS}
    PhysicsWorld2D.cpp
)
add_library(Tina::Physics2D ALIAS tina_physics2d)

target_compile_features(tina_physics2d PUBLIC cxx_std_23)
target_include_directories(tina_physics2d PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_physics2d
    PUBLIC Tina::Core
    PRIVATE Tina::ProjectOptions box2d::box2d
)
set_target_properties(tina_physics2d PROPERTIES CXX_EXTENSIONS OFF)
