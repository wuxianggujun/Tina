add_library(tina_ai_header_isolation OBJECT
    header_isolation/BlackboardHeader.cpp
    header_isolation/BehaviorTreeHeader.cpp
    header_isolation/StateMachineHeader.cpp
)
target_compile_features(tina_ai_header_isolation PRIVATE cxx_std_23)
target_include_directories(tina_ai_header_isolation PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_ai_header_isolation PRIVATE Tina::ProjectOptions)
set_target_properties(tina_ai_header_isolation PROPERTIES CXX_EXTENSIONS OFF)

add_executable(tina_ai_tests AITests.cpp $<TARGET_OBJECTS:tina_ai_header_isolation>)
target_compile_features(tina_ai_tests PRIVATE cxx_std_23)
target_link_libraries(tina_ai_tests PRIVATE Tina::ProjectOptions Tina::GameSDK GTest::gtest_main)
set_target_properties(tina_ai_tests PROPERTIES CXX_EXTENSIONS OFF)
