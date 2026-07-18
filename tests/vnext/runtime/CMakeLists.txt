add_executable(tina_runtime_ui_tests
    PrimaryWindowUICapabilityTests.cpp
    PrimaryWindowUIContextOwnerTests.cpp
    UIInputRouteProducerTests.cpp
)

target_compile_features(tina_runtime_ui_tests PRIVATE cxx_std_23)
target_link_libraries(tina_runtime_ui_tests
    PRIVATE
        Tina::ProjectOptions
        Tina::Core
        Tina::Platform
        Tina::Runtime
        Tina::UI
        GTest::gtest_main
)
set_target_properties(tina_runtime_ui_tests PROPERTIES CXX_EXTENSIONS OFF)
