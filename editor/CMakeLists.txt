# The editor tree. Reached only when TINA_BUILD_EDITOR is on.
#
# tina_editor is headless authoring logic and builds anywhere. The desktop shell needs a window and a
# GPU, so app/ and its tests stay behind the same GLFW+bgfx guard the rest of the desktop graph uses.
add_subdirectory(src)

if(TINA_BUILD_PLATFORM_GLFW AND TINA_BUILD_RENDER_BGFX)
    add_subdirectory(app)
endif()

if(TINA_BUILD_TESTING)
    add_subdirectory(tests)
endif()
