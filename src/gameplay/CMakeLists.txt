set(TINA_GAMEPLAY_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/gameplay/Action.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/gameplay/Easing.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/gameplay/GameplayErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/gameplay/GameplayTypes.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/gameplay/Scheduler.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/gameplay/Signal.hpp
)

add_library(tina_gameplay STATIC
    ${TINA_GAMEPLAY_PUBLIC_HEADERS}
    Action.cpp
    ActionProgram.hpp
    ActionRunner.cpp
    Scheduler.cpp
    Signal.cpp
)
add_library(Tina::Gameplay ALIAS tina_gameplay)

target_compile_features(tina_gameplay PUBLIC cxx_std_23)
target_include_directories(tina_gameplay
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Core for Result/Duration/GenerationPool/MoveOnlyFunction, Math because the typed
# tween overloads name Vec2/3/4 in the public header. Deliberately nothing else:
# this layer schedules time and delivers callbacks, so it must not know about
# Scene, Asset, Physics or UI -- a game that wants to tween a transform passes a
# setter that writes it, which is what keeps the same tween usable for a UI value,
# an audio gain or a plain gameplay float.
target_link_libraries(tina_gameplay
    PUBLIC Tina::Core Tina::Math
    PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>"
)
set_target_properties(tina_gameplay PROPERTIES CXX_EXTENSIONS OFF)
