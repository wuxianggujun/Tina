add_subdirectory(null)
add_subdirectory(2d)
add_subdirectory(3d)

if(TINA_BUILD_PLATFORM_GLFW)
    add_subdirectory(platform)
endif()

if(TINA_BUILD_PLATFORM_GLFW AND TINA_BUILD_RENDER_BGFX)
    add_subdirectory(desktop)
    add_subdirectory(3d_bgfx)
endif()
