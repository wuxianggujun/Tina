add_library(tina_platform STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/platform/Input.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/platform/PlatformBackend.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/platform/PlatformErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/platform/PlatformFrame.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/platform/Window.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/platform/headless/HeadlessPlatformFactory.hpp
    headless/HeadlessPlatform.cpp
)
add_library(Tina::Platform ALIAS tina_platform)

target_compile_features(tina_platform PUBLIC cxx_std_23)
target_include_directories(tina_platform PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_platform PUBLIC Tina::Core)
target_link_libraries(tina_platform PRIVATE Tina::ProjectOptions)
set_target_properties(tina_platform PROPERTIES CXX_EXTENSIONS OFF)

if(TINA_BUILD_PLATFORM_GLFW)
    add_subdirectory(glfw)
endif()
