if(TINA_BUILD_LEGACY OR TINA_BUILD_RENDER_BGFX OR TINA_BUILD_SHADERS)
    # bgfx stays on its upstream source revision because the vcpkg port trails the renderer version
    # required by Tina. Its nested bgfx/bx/bimg repositories are managed by bgfx.cmake itself.
    set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
    set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS ${TINA_BUILD_SHADERS} CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_SHADER ${TINA_BUILD_SHADERS} CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "" FORCE)
    set(BGFX_CONFIG_VIDEO OFF CACHE BOOL "" FORCE)
    set(BGFX_WITH_WAYLAND ${TINA_BUILD_WAYLAND} CACHE BOOL "" FORCE)
    add_subdirectory(bgfx.cmake)
endif()

# EASTL and EABase remain source dependencies until Tina's allocator bridge and public container
# aliases can be migrated independently and verified under sanitizers.
# EABase still declares a pre-3.5 CMake compatibility level. CMake 4 requires the consuming
# project to opt into the modern minimum without patching the upstream submodule.
if(TINA_BUILD_LEGACY)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    set(FETCHCONTENT_SOURCE_DIR_EABASE "${CMAKE_CURRENT_SOURCE_DIR}/EABase" CACHE PATH "" FORCE)
    set(EASTL_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
    set(EASTL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(EASTL)
endif()
