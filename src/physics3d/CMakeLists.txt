tina_add_runtime_module(tina_physics3d PhysicsWorld3D.cpp)
add_library(Tina::Physics3D ALIAS tina_physics3d)
target_compile_features(tina_physics3d PUBLIC cxx_std_23)
target_include_directories(tina_physics3d PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_physics3d
    PUBLIC Tina::Core Tina::Math
    PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>" Jolt::Jolt
)
set_target_properties(tina_physics3d PROPERTIES CXX_EXTENSIONS OFF)
