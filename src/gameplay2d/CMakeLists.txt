set(TINA_GAMEPLAY2D_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/gameplay2d/Scene2DPhysicsBridge.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/gameplay2d/Scene2DRuntime.hpp
)

add_library(tina_gameplay2d STATIC
    ${TINA_GAMEPLAY2D_PUBLIC_HEADERS}
    Scene2DPhysicsBridge.cpp
    Scene2DRuntime.cpp
)
add_library(Tina::Gameplay2D ALIAS tina_gameplay2d)

target_include_directories(tina_gameplay2d
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# This module exists because the subsystems it joins are deliberately blind to
# each other: Scene must not pull Box2D or Asset into every consumer, and
# Physics2D/Audio/Navigation2D each link only Core. Gameplay2D is the single place
# where all of them are visible, which is what lets one owner enforce the
# per-frame ordering and lease lifetime that every game would otherwise rewrite
# (ADR 0031). Asset transitively provides Navigation2D and Physics2D.
target_link_libraries(tina_gameplay2d
    PUBLIC Tina::Core Tina::Math Tina::Scene Tina::Physics2D Tina::Asset Tina::Audio
    PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>"
)
set_target_properties(tina_gameplay2d PROPERTIES CXX_EXTENSIONS OFF)
