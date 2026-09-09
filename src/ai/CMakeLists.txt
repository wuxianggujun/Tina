tina_add_runtime_module(tina_ai
    ${PROJECT_SOURCE_DIR}/include/tina/ai/AIErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ai/Blackboard.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ai/BehaviorTree.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ai/StateMachine.hpp
    Blackboard.cpp
    BehaviorTree.cpp
    StateMachine.cpp
    DispatchGuard.hpp
)
add_library(Tina::AI ALIAS tina_ai)
target_compile_features(tina_ai PUBLIC cxx_std_23)
target_include_directories(tina_ai PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_ai PUBLIC Tina::Core Tina::Math PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>")
set_target_properties(tina_ai PROPERTIES CXX_EXTENSIONS OFF)
