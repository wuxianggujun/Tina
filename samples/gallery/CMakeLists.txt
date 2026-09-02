# The worked example of the content/frontend split, and the reason the split is enforced:
# the Android JNI bridge links this content library directly, so the scenes reach a device
# without being rewritten. Twelve other samples cannot, because their content and their
# GLFW composition root share one translation unit.
#
# tina_add_game_content refuses a platform link outright, which is what keeps that true as
# scenes are added.
tina_add_game_content(tina_sample_gallery
    SOURCES
        GalleryActions.hpp
        GalleryActions.cpp
        GalleryScene.hpp
        GalleryMenu.cpp
        GalleryScenes.cpp
)
add_library(Tina::SampleGallery ALIAS tina_sample_gallery)

# The desktop front-end. Only built when GLFW is available, like every other windowed sample.
if(TINA_BUILD_PLATFORM_GLFW AND TINA_BUILD_RENDER_BGFX)
    tina_add_desktop_frontend(tina_sample_gallery_desktop
        SOURCES DesktopMain.cpp
        CONTENT Tina::SampleGallery
    )
endif()

# Checked after the frontend exists, so the walk sees the finished graph. This is what
# catches an indirect platform dependency -- content linking a helper that itself links
# GLFW passes the direct LINK check and still cannot build for Android.
tina_verify_game_content_portable(tina_sample_gallery)
