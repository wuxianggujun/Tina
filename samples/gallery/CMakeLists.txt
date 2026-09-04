add_subdirectory(core)

if(TINA_BUILD_PLATFORM_GLFW AND TINA_BUILD_RENDER_BGFX)
    add_subdirectory(platforms/desktop)
endif()

# Checked after the frontend exists, so the walk sees the finished graph. This is what catches an
# indirect platform dependency -- content linking a helper that itself links GLFW passes the
# direct LINK check and still cannot build for Android.
tina_verify_game_content_portable(tina_sample_gallery)
