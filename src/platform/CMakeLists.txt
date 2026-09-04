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
target_link_libraries(tina_platform PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>")
set_target_properties(tina_platform PROPERTIES CXX_EXTENSIONS OFF)

if(TINA_BUILD_PLATFORM_GLFW)
    add_subdirectory(glfw)
endif()

# Gated on the target platform rather than an option: this adapter is only buildable when
# targeting Android, so an option would advertise a choice that does not exist. GLFW is an
# option because a desktop build can legitimately go without it.
if(ANDROID)
    add_subdirectory(android)
endif()

# Same reasoning as Android: this adapter binds to a browser canvas through
# Emscripten's html5.h, so it is only buildable when targeting Emscripten.
if(EMSCRIPTEN)
    add_subdirectory(html5)
endif()

# Unlike Android and HTML5, this adapter carries no Apple SDK dependency: the host hands a
# CAMetalLayer across as an opaque integer. Building it on every host is what lets the contract
# tests actually run, rather than only compiling under a toolchain nobody here has. Gating it on
# IOS would advertise a choice that does not exist for the C++ layer -- the ObjC host is a
# separate, later slice.
add_subdirectory(ios)

