# Only configured when targeting Android, so on this host it is compile-verified by the cross
# build rather than executed: there is no device runner wired up, and no Android platform backend
# consumer that could host one. Every case here is a pure contract assertion on the factory and
# the seven virtuals -- no window, GPU or JNI -- so it will pass unchanged once a device or
# emulator runner exists.
add_executable(tina_platform_android_tests
    AndroidBackendContractTests.cpp
    AndroidInputBridgeTests.cpp
)

target_compile_features(tina_platform_android_tests PRIVATE cxx_std_23)
target_include_directories(tina_platform_android_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/integration
)
target_link_libraries(tina_platform_android_tests PRIVATE
    Tina::ProjectOptions
    Tina::PlatformAndroid
    Tina::WindowSurfaceIntegration
    GTest::gtest_main
)
set_target_properties(tina_platform_android_tests PROPERTIES CXX_EXTENSIONS OFF)
