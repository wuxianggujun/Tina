# bgfx stays on its upstream source revision because the vcpkg port trails the renderer version
# required by Tina. Nested bgfx/bx/bimg repositories are managed by bgfx.cmake itself.
# The vNext bgfx backend cooks its private shader set into build-tree headers.
if(TINA_BUILD_RENDER_BGFX OR TINA_BUILD_SHADERS)
    set(TINA_NEEDS_BGFX_SHADERC ON)
    set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
    set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS ${TINA_NEEDS_BGFX_SHADERC} CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_SHADER ${TINA_NEEDS_BGFX_SHADERC} CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "" FORCE)
    set(BGFX_CONFIG_VIDEO OFF CACHE BOOL "" FORCE)
    set(BGFX_WITH_WAYLAND ${TINA_BUILD_WAYLAND} CACHE BOOL "" FORCE)
    add_subdirectory(bgfx.cmake)

    if(TINA_NEEDS_BGFX_SHADERC AND MSVC AND TARGET shaderc)
        target_compile_options(shaderc PRIVATE /MP4)
    endif()
endif()

# EASTL/EABase retired with Legacy product (no remaining consumers).
