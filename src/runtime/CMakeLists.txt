add_library(tina_runtime STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/EngineConfig.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/EngineHost.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/FrameTiming.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/GameApplication.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/GameState.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/GameStateCommands.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/InputActions.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/InputActionMap.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/PhaseContexts.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/PlatformEvents.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/PrimaryWindowUI.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/RunExitReason.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/RuntimeErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/spi/EngineCompositionFactories.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/integration/WindowSurface.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/spi/PlatformEventDispatcher.hpp
    EngineConfig.cpp
    EngineHost.cpp
    PhaseContexts.cpp
    PrimaryWindowUI.cpp
    PlatformEventDispatcher.cpp
    input/ActionMapper.cpp
    input/ActionMapper.hpp
    input/LastPresentedCamera2DLatch.cpp
    input/LastPresentedCamera2DLatch.hpp
    input/SimulationActionLatch.cpp
    input/SimulationActionLatch.hpp
    input/UIInputRouteProducer.cpp
    input/UIInputRouteProducer.hpp
    ui/PrimaryWindowUIContextOwner.cpp
    ui/PrimaryWindowUIContextOwner.hpp
    ui/PrimaryWindowUICapabilityState.cpp
    ui/PrimaryWindowUICapabilityState.hpp
    ui/PrimaryWindowUIDisplayCoordinator.cpp
    ui/PrimaryWindowUIDisplayCoordinator.hpp
    ui/PrimaryWindowUILayoutCoordinator.cpp
    ui/PrimaryWindowUILayoutCoordinator.hpp
)
add_library(Tina::Runtime ALIAS tina_runtime)

target_compile_features(tina_runtime PUBLIC cxx_std_23)
target_include_directories(tina_runtime
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(tina_runtime
    PUBLIC
        Tina::Core
        Tina::Platform
        Tina::Task
        Tina::Render
        Tina::WindowSurfaceIntegration
        Tina::UI
        Tina::Audio
    PRIVATE
        Tina::ProjectOptions
        Tina::UIRenderIntegration
)
if(TINA_BUILD_UI_UIA)
    target_compile_definitions(tina_runtime PRIVATE TINA_HAS_UI_UIA=1)
    target_include_directories(tina_runtime PRIVATE "${PROJECT_SOURCE_DIR}/src/ui/uia")
    target_link_libraries(tina_runtime PRIVATE Tina::UIUia)
endif()
set_target_properties(tina_runtime PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME Runtime
)
