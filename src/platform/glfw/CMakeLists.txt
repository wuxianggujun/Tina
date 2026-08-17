add_library(tina_platform_glfw STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/platform/glfw/GlfwPlatformFactory.hpp
    GlfwDigitalFocusFilter.hpp
    GlfwGamepadTranslation.hpp
    GlfwGamepadTranslation.cpp
    GlfwInputTranslation.hpp
    GlfwInputTranslation.cpp
    GlfwNativeWindowBinding.hpp
    GlfwNativeWindowBinding.cpp
    GlfwTextInputPlacement.hpp
    GlfwTextInputPlacement.cpp
    GlfwPlatformBackend.cpp
)

if(TINA_BUILD_TESTING)
    target_sources(tina_platform_glfw PRIVATE GlfwBackendTestAccess.hpp)
    target_compile_definitions(tina_platform_glfw PRIVATE TINA_PLATFORM_GLFW_ENABLE_TEST_ACCESS=1)
endif()

# Session logic is backend-neutral and unit-tested on all platforms.
target_sources(tina_platform_glfw PRIVATE Imm32CompositionSession.cpp)
if(WIN32)
    target_sources(tina_platform_glfw PRIVATE
        GlfwNativeWindowBindingWin32.cpp
        Imm32CompositionHostWin32.cpp
    )
elseif(UNIX AND NOT APPLE)
    target_sources(tina_platform_glfw PRIVATE GlfwNativeWindowBindingX11.cpp)
    if(TINA_BUILD_WAYLAND)
        target_sources(tina_platform_glfw PRIVATE GlfwNativeWindowBindingWayland.cpp)
        target_compile_definitions(tina_platform_glfw PRIVATE TINA_GLFW_ENABLE_WAYLAND=1)
    endif()
endif()
add_library(Tina::PlatformGlfw ALIAS tina_platform_glfw)

target_compile_features(tina_platform_glfw PUBLIC cxx_std_23)
target_include_directories(tina_platform_glfw
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src/integration
)
target_link_libraries(tina_platform_glfw
    PUBLIC
        Tina::Platform
        Tina::WindowSurfaceIntegration
    PRIVATE
        $<BUILD_INTERFACE:Tina::ProjectOptions>
        glfw
)
if(WIN32)
    target_link_libraries(tina_platform_glfw PRIVATE imm32)
endif()
set_target_properties(tina_platform_glfw PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME PlatformGlfw
)
