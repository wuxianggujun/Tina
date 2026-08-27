add_subdirectory(null)
add_subdirectory(asset)
add_subdirectory(2d)
add_subdirectory(2d_tilemap)
add_subdirectory(3d)

if (TINA_BUILD_PHYSICS2D)
    add_subdirectory(physics2d_bench)
    # Tina::Gameplay2D only exists with Physics2D, and this sample is its first
    # consumer outside the tests. Headless, so it needs no GPU or window.
    add_subdirectory(2d_authored_scene)
endif ()

if(TINA_BUILD_PLATFORM_GLFW)
    add_subdirectory(platform)
endif()

if(TINA_BUILD_PLATFORM_GLFW AND TINA_BUILD_RENDER_BGFX)
    add_subdirectory(desktop)
    add_subdirectory(ui_showcase)
    add_subdirectory(desktop_shell)
    # On-screen thumbstick driven by pointer drag or WASD. Needs a real window and
    # UI backend, so it lives with the other desktop-only samples.
    add_subdirectory(virtual_stick)
    add_subdirectory(3d_bgfx)
    add_subdirectory(3d_product)
    add_subdirectory(2d_bgfx)
    add_subdirectory(2d_catalog)
    add_subdirectory(2d_tilemap_bgfx)
endif()
