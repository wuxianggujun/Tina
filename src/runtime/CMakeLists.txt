add_library(tina_runtime STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/EngineConfig.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/EngineHost.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/FrameTiming.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/GameApplication.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/GameState.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/PhaseContexts.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/RunExitReason.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/RuntimeErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/runtime/spi/EngineFactories.hpp
    EngineConfig.cpp
    EngineHost.cpp
    PhaseContexts.cpp
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
    PRIVATE
        Tina::ProjectOptions
)
set_target_properties(tina_runtime PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME Runtime
)
