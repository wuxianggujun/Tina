add_subdirectory(editor)

if(TINA_BUILD_PLATFORM_GLFW AND TINA_BUILD_RENDER_BGFX)
    add_subdirectory(editor_app)
endif()
