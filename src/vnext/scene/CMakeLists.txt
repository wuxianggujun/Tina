set(TINA_SCENE_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/scene/Entity.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/Scene.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/SceneErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/Transform.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/scene/World.hpp
)

add_library(tina_scene STATIC
    ${TINA_SCENE_PUBLIC_HEADERS}
    World.cpp
)
add_library(Tina::Scene ALIAS tina_scene)

target_compile_features(tina_scene PUBLIC cxx_std_23)
target_include_directories(tina_scene PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_scene
    PUBLIC Tina::Core
    PRIVATE Tina::ProjectOptions
)
set_target_properties(tina_scene PROPERTIES CXX_EXTENSIONS OFF)
