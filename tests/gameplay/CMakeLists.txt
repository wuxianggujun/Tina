set(TINA_GAMEPLAY_HEADER_ISOLATION_SOURCES
    header_isolation/ActionHeader.cpp
    header_isolation/EasingHeader.cpp
    header_isolation/GameplayErrorsHeader.cpp
    header_isolation/GameplayTypesHeader.cpp
    header_isolation/SchedulerHeader.cpp
    header_isolation/SignalHeader.cpp
)

# One TU per public header, compiled with nothing else included, so a header that
# silently relies on another module's include order fails here rather than in the
# first consumer that happens to include them in a different order.
add_library(tina_gameplay_header_isolation OBJECT
    ${TINA_GAMEPLAY_HEADER_ISOLATION_SOURCES}
)
target_compile_features(tina_gameplay_header_isolation PRIVATE cxx_std_23)
target_include_directories(tina_gameplay_header_isolation PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(tina_gameplay_header_isolation PRIVATE
    Tina::ProjectOptions
)
set_target_properties(tina_gameplay_header_isolation PROPERTIES CXX_EXTENSIONS OFF)

add_executable(tina_gameplay_tests
    ActionAuthoringTests.cpp
    ActionRunnerTests.cpp
    EasingTests.cpp
    SchedulerTests.cpp
    SignalTests.cpp
    $<TARGET_OBJECTS:tina_gameplay_header_isolation>
)
target_compile_features(tina_gameplay_tests PRIVATE cxx_std_23)
# Core and Math arrive through Tina::Gameplay's PUBLIC link, which is also what a
# game gets, so the tests compile against the same include set a consumer does.
target_link_libraries(tina_gameplay_tests PRIVATE
    Tina::ProjectOptions
    Tina::Gameplay
    GTest::gtest_main
)
set_target_properties(tina_gameplay_tests PROPERTIES CXX_EXTENSIONS OFF)
