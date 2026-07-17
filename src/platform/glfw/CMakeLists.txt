add_library(tina_platform_glfw STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/platform/glfw/GlfwPlatformFactory.hpp
    GlfwBackendTestAccess.hpp
    GlfwDigitalFocusFilter.hpp
    GlfwInputTranslation.hpp
    GlfwInputTranslation.cpp
    GlfwPlatformBackend.cpp
)
add_library(Tina::PlatformGlfw ALIAS tina_platform_glfw)

target_compile_features(tina_platform_glfw PUBLIC cxx_std_23)
target_include_directories(tina_platform_glfw
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(tina_platform_glfw
    PUBLIC
        Tina::Platform
    PRIVATE
        Tina::ProjectOptions
        glfw
)
set_target_properties(tina_platform_glfw PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME PlatformGlfw
)
