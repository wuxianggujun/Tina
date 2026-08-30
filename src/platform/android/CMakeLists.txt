add_library(tina_platform_android STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/platform/android/AndroidInputBridge.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/platform/android/AndroidPlatformFactory.hpp
    AndroidCompositionSession.hpp
    AndroidCompositionSession.cpp
    AndroidInputBridge.cpp
    AndroidPlatform.cpp
)
add_library(Tina::PlatformAndroid ALIAS tina_platform_android)

target_compile_features(tina_platform_android PUBLIC cxx_std_23)
target_include_directories(tina_platform_android
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src/integration
)
# No libandroid, no JNI, and no Android SDK header: the host hands the ANativeWindow across as
# an opaque integer, so this adapter has the same dependency footprint as Headless. That is
# also why it needs no third-party link -- once a JNI input bridge exists it will, and the
# dependency belongs on that slice rather than being reserved here.
target_link_libraries(tina_platform_android
    PUBLIC
        Tina::Platform
        Tina::WindowSurfaceIntegration
    PRIVATE
        $<BUILD_INTERFACE:Tina::ProjectOptions>
)
set_target_properties(tina_platform_android PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME PlatformAndroid
)

# Separate target because it is the only thing that links libandroid and JNI. Keeping it out of
# tina_platform_android is what lets the backend stay dependency-free and testable without a JVM.
add_subdirectory(jni)
