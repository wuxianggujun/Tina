set(TINA_GAMEPLAY2D_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/gameplay2d/Scene2DRuntime.hpp
)
set(TINA_GAMEPLAY2D_SOURCES
    Scene2DRuntime.cpp
)

if (TINA_BUILD_PHYSICS2D)
    list(APPEND TINA_GAMEPLAY2D_PUBLIC_HEADERS
        ${PROJECT_SOURCE_DIR}/include/tina/gameplay2d/Scene2DPhysicsBridge.hpp)
    list(APPEND TINA_GAMEPLAY2D_SOURCES
        Scene2DPhysicsBridge.cpp)
endif ()

add_library(tina_gameplay2d STATIC
    ${TINA_GAMEPLAY2D_PUBLIC_HEADERS}
    ${TINA_GAMEPLAY2D_SOURCES}
)
add_library(Tina::Gameplay2D ALIAS tina_gameplay2d)

target_include_directories(tina_gameplay2d
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# This module exists because the subsystems it joins are deliberately blind to
# each other: Scene must not pull Box2D or Asset into every consumer, and
# Audio/Navigation2D each link only Core. Gameplay2D is the single place where
# they are visible, which is what lets one owner enforce the per-frame ordering
# and lease lifetime that every game would otherwise rewrite (ADR 0031).
# Asset transitively provides Navigation2D. Physics2D is optional: the runtime
# still instantiates authored TileMap/Fx/Navigation/Audio nodes without it.
target_link_libraries(tina_gameplay2d
    PUBLIC Tina::Core Tina::Math Tina::Scene Tina::Asset Tina::Audio
    PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>"
)
if (TINA_BUILD_PHYSICS2D)
    # PUBLIC so consumers of Scene2DRuntime.hpp see TINA_HAS_PHYSICS2D and the
    # 4-arg build() / physicsBridge() surface. Mirrors TINA_HAS_UI_UIA, except
    # that one stays PRIVATE because UIA never leaks into a public header.
    target_compile_definitions(tina_gameplay2d PUBLIC TINA_HAS_PHYSICS2D=1)
    target_link_libraries(tina_gameplay2d PUBLIC Tina::Physics2D)
endif ()
set_target_properties(tina_gameplay2d PROPERTIES CXX_EXTENSIONS OFF)
