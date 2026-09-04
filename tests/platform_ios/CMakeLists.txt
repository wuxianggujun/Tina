# Built whenever tina_platform_ios is, which is every host: the adapter has no Apple SDK
# dependency, so these are ordinary unit tests rather than a cross-compile-only compile check.
add_executable(tina_platform_ios_tests
    IosBackendContractTests.cpp
    IosCompositionSessionTests.cpp
    IosInputBridgeTests.cpp
    IosSessionTests.cpp
)

target_compile_features(tina_platform_ios_tests PRIVATE cxx_std_23)
target_include_directories(tina_platform_ios_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/platform/ios
    ${PROJECT_SOURCE_DIR}/src/integration
)
target_link_libraries(tina_platform_ios_tests PRIVATE
    Tina::ProjectOptions
    Tina::PlatformIos
    Tina::WindowSurfaceIntegration
    GTest::gtest_main
)
set_target_properties(tina_platform_ios_tests PROPERTIES CXX_EXTENSIONS OFF)
