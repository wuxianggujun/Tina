add_executable(tina_platform_glfw_tests
    GlfwDigitalFocusFilterTests.cpp
    GlfwGamepadTranslationTests.cpp
    GlfwInputTranslationTests.cpp
    Imm32CompositionSessionTests.cpp
    GlfwBackendIntegrationTests.cpp
)

target_compile_features(tina_platform_glfw_tests PRIVATE cxx_std_23)
target_include_directories(tina_platform_glfw_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/platform/glfw
    ${PROJECT_SOURCE_DIR}/src/integration
)
target_link_libraries(tina_platform_glfw_tests PRIVATE
    Tina::ProjectOptions
    Tina::PlatformGlfw
    Tina::WindowSurfaceIntegration
    GTest::gtest_main
    glfw
)
set_target_properties(tina_platform_glfw_tests PROPERTIES CXX_EXTENSIONS OFF)

if(WIN32)
    add_custom_command(TARGET tina_platform_glfw_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:tina_platform_glfw_tests>
            $<TARGET_FILE_DIR:tina_platform_glfw_tests>
        COMMAND_EXPAND_LISTS
    )
endif()
