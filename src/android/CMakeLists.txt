tina_add_runtime_module(tina_bootstrap_android
    ${PROJECT_SOURCE_DIR}/include/tina/android/AndroidEngine.hpp
    AndroidEngine.cpp
)
add_library(Tina::AndroidBootstrap ALIAS tina_bootstrap_android)
target_compile_features(tina_bootstrap_android PUBLIC cxx_std_23)
target_include_directories(tina_bootstrap_android
    PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
    PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tina_bootstrap_android
    PUBLIC Tina::Runtime
    PRIVATE Tina::PlatformAndroid Tina::RenderBgfx Tina::Task
        "$<BUILD_INTERFACE:Tina::ProjectOptions>"
        EGL GLESv2 android log)
if(TINA_BUILD_UI_FREETYPE)
    target_compile_definitions(tina_bootstrap_android PRIVATE TINA_HAS_UI_FREETYPE=1)
    target_link_libraries(tina_bootstrap_android PRIVATE Tina::UIFreetype)
endif()
set_target_properties(tina_bootstrap_android PROPERTIES CXX_EXTENSIONS OFF)
