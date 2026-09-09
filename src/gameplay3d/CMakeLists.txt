set(TINA_GAMEPLAY3D_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/gameplay3d/Scene3DRuntime.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/gameplay3d/World3DSceneIndex.hpp
)
tina_add_runtime_module(tina_gameplay3d
    ${TINA_GAMEPLAY3D_PUBLIC_HEADERS}
    Scene3DRuntime.cpp
    World3DSceneIndex.cpp
)
add_library(Tina::Gameplay3D ALIAS tina_gameplay3d)
target_compile_features(tina_gameplay3d PUBLIC cxx_std_23)
target_include_directories(tina_gameplay3d PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(tina_gameplay3d PUBLIC Tina::Core Tina::Math Tina::Scene Tina::Asset Tina::Animation3D
    PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>")
if (TINA_BUILD_PHYSICS3D)
    target_sources(tina_gameplay3d PRIVATE Scene3DPhysicsBridge.cpp
        ${PROJECT_SOURCE_DIR}/include/tina/gameplay3d/Scene3DPhysicsBridge.hpp)
    target_compile_definitions(tina_gameplay3d PUBLIC TINA_HAS_PHYSICS3D=1)
    target_link_libraries(tina_gameplay3d PUBLIC Tina::Physics3D)
endif ()
set_target_properties(tina_gameplay3d PROPERTIES CXX_EXTENSIONS OFF)
