set(TINA_GAMEPLAY2D_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/gameplay2d/Scene2DPhysicsBridge.hpp
)

add_library(tina_gameplay2d STATIC
    ${TINA_GAMEPLAY2D_PUBLIC_HEADERS}
    Scene2DPhysicsBridge.cpp
)
add_library(Tina::Gameplay2D ALIAS tina_gameplay2d)

target_include_directories(tina_gameplay2d
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# The bridge exists because neither Scene nor Physics2D may depend on the other:
# Scene must not pull Box2D into every consumer, and Physics2D must stay unaware
# of Scene. This module is the only place both are visible.
target_link_libraries(tina_gameplay2d
    PUBLIC Tina::Core Tina::Scene Tina::Physics2D
    PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>"
)
set_target_properties(tina_gameplay2d PROPERTIES CXX_EXTENSIONS OFF)
