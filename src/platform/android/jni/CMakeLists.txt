# Shared, unlike every other Tina target: Java loads it with System.loadLibrary, which needs a .so.
add_library(tina_platform_android_jni SHARED
    TinaAndroidJni.cpp
)
add_library(Tina::PlatformAndroidJni ALIAS tina_platform_android_jni)

target_compile_features(tina_platform_android_jni PUBLIC cxx_std_23)
target_link_libraries(tina_platform_android_jni
    PRIVATE
        $<BUILD_INTERFACE:Tina::ProjectOptions>
        Tina::PlatformAndroid
        Tina::WindowSurfaceIntegration
        # The only Android SDK dependency in the whole engine, and it lives here rather than in
        # tina_platform_android: ANativeWindow_fromSurface needs libandroid, but the backend itself
        # takes the window as an opaque integer and must stay dependency-free and host-testable.
        android
        log
)

# Optional so an APK can be built without a host shaderc. With bgfx the APK actually draws; without
# it the platform bridge still runs and the screen stays blank.
#
# Keyed on the option rather than `if(TARGET tina_render_bgfx)`: src/platform is added before
# src/render, so that target does not exist yet here. The target check silently evaluated false and
# produced a renderer-less library that still linked and ran -- caught only by counting bgfx symbols
# in the .so.
if(TINA_BUILD_RENDER_BGFX)
    target_compile_definitions(tina_platform_android_jni PRIVATE TINA_ANDROID_WITH_BGFX=1)
    # The game application only exists on the renderer path, because EngineHost requires a render
    # device. Without bgfx there is nothing for it to run in.
    target_sources(tina_platform_android_jni PRIVATE
        TinaAndroidGame.hpp
        TinaAndroidGame.cpp
    )
    # Tina::SampleGallery is linked, not reimplemented: the menu and scenes are the same translation
    # units the desktop front-end uses. That library deliberately links no GLFW, which is what makes
    # it reachable from here at all. Other samples' content libraries are equally reachable now that
    # every sample splits core/ from platforms/; what is not reachable is any sample whose whole
    # program is its GLFW composition root, since there is no content half to link.
    # Tina::Asset is here because the demo loads the catalog the APK ships. It is not pulled in
    # transitively: tina_runtime links Core/Platform/Task/Render and deliberately not Asset, since the
    # engine host has no opinion about content.
    target_link_libraries(tina_platform_android_jni PRIVATE Tina::Runtime Tina::Asset tina_render_bgfx
                                                           Tina::SampleGallery)
    # The concrete backend header is private to src/, reached the same way a desktop composition root
    # reaches it. It never appears in a public header.
    target_include_directories(tina_platform_android_jni PRIVATE ${PROJECT_SOURCE_DIR}/src)
    # Real glyphs instead of solid blocks. Without the FreeType adapter the UI still runs -- it falls back
    # to drawing each glyph as a filled quad -- but every label is unreadable, which makes a menu of named
    # scenes pointless. Gated on the option so a checkout without the vcpkg feature still builds.
    if(TINA_BUILD_UI_FREETYPE)
        target_compile_definitions(tina_platform_android_jni PRIVATE TINA_HAS_UI_FREETYPE=1)
        target_link_libraries(tina_platform_android_jni PRIVATE tina_ui_freetype)
    endif()

    # bgfx's GLES renderer calls EGL and GLESv2 directly, and its Vulkan renderer loads libvulkan at
    # runtime rather than linking it. Both GLES libraries are needed even when Vulkan is preferred,
    # because the GLES path stays compiled in as the fallback for devices whose Vulkan driver is
    # unusable.
    target_link_libraries(tina_platform_android_jni PRIVATE EGL GLESv2)
endif()
set_target_properties(tina_platform_android_jni PROPERTIES
    CXX_EXTENSIONS OFF
    # Java resolves this exact name in System.loadLibrary("tina_android").
    OUTPUT_NAME tina_android
)
