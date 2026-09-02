# bgfx stays on its upstream source revision because the vcpkg port trails the renderer version
# required by Tina. Nested bgfx/bx/bimg repositories are managed by bgfx.cmake itself.
# The vNext bgfx backend cooks its private shader set into build-tree headers.
if(TINA_BUILD_RENDER_BGFX OR TINA_BUILD_SHADERS)
    # shaderc only ever runs on the build host. When one is supplied from a host build tree
    # (the cross-compiling case), building the in-tree copy is pure waste: upstream declares it
    # with a plain add_executable(), so it would be built for the target -- measured at 460 MB
    # of AArch64 ELF that the host cannot execute. See cmake/TinaBgfxEmbeddedShaders.cmake.
    if(TINA_BGFX_SHADERC_EXECUTABLE)
        set(TINA_NEEDS_BGFX_SHADERC OFF)
    else()
        set(TINA_NEEDS_BGFX_SHADERC ON)
    endif()
    set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    # Tina installs only the bgfx runtime closure required by RenderBgfx. The
    # upstream install graph also exports shader tools and image codecs.
    set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
    set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS ${TINA_NEEDS_BGFX_SHADERC} CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_SHADER ${TINA_NEEDS_BGFX_SHADERC} CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "" FORCE)
    # Compiles bgfx's hardware video decoders in. This is a compile-time gate: with it
    # off, IRenderDevice::videoDecodeCapabilities() can only ever report "unsupported",
    # regardless of what the GPU can do. On Windows and Linux it adds no link
    # dependency; Apple platforms pull in VideoToolbox/CoreMedia/CoreVideo.
    set(BGFX_CONFIG_VIDEO ON CACHE BOOL "" FORCE)
    set(BGFX_WITH_WAYLAND ${TINA_BUILD_WAYLAND} CACHE BOOL "" FORCE)
    add_subdirectory(bgfx.cmake)

    # l-smash is the MP4 demuxer for tina_assetc. Upstream only creates it together
    # with texturev, whose image/tool closure Tina deliberately does not build.
    set(TINA_LSMASH_DIR "${PROJECT_SOURCE_DIR}/thirdparty/bgfx.cmake/bgfx/3rdparty/l-smash")
    file(GLOB_RECURSE TINA_LSMASH_SOURCES CONFIGURE_DEPENDS
        "${TINA_LSMASH_DIR}/*.c"
        "${TINA_LSMASH_DIR}/*.h"
    )
    add_library(tina_lsmash STATIC ${TINA_LSMASH_SOURCES})
    target_include_directories(tina_lsmash
        PUBLIC "${PROJECT_SOURCE_DIR}/thirdparty/bgfx.cmake/bgfx/3rdparty"
        PRIVATE "${TINA_LSMASH_DIR}"
    )
    if(MSVC)
        target_compile_options(tina_lsmash PRIVATE
            /wd4005 /wd4018 /wd4100 /wd4116 /wd4125 /wd4133 /wd4210 /wd4244
            /wd4245 /wd4267 /wd4389 /wd4701
        )
    else()
        target_compile_options(tina_lsmash PRIVATE
            -Wno-empty-body -Wno-implicit-fallthrough -Wno-missing-field-initializers
            -Wno-pragmas -Wno-sign-compare -Wno-type-limits -Wno-undef
            -Wno-unused-function -Wno-unused-parameter
        )
    endif()
    if(MINGW)
        target_compile_options(tina_lsmash PRIVATE -Wno-maybe-uninitialized -Wno-stringop-overflow)
    endif()

    # Keep Tina's bundled runtime closure distinct from any bgfx package that
    # the consumer may already use.
    add_library(TinaBgfxRuntime::bgfx ALIAS bgfx)
    add_library(TinaBgfxRuntime::bx ALIAS bx)
    add_library(TinaBgfxRuntime::bimg ALIAS bimg)
    add_library(TinaBgfxRuntime::l-smash ALIAS tina_lsmash)

    # bimg_decode/bimg_encode are the texture codec libraries. Upstream's bimg/CMakeLists.txt
    # creates them unconditionally, but the only thing that links them is texturec, which
    # BGFX_BUILD_TOOLS_TEXTURE=OFF above already excludes -- so nothing consumes them and
    # Tina aliases only bimg itself.
    #
    # They still have to leave the default build, not merely go unlinked: bimg_encode's
    # etcpak sources include x86 intrinsic headers unconditionally, so `cmake --build` with
    # no --target fails on wasm even though every Tina target is fine. Excluding them keeps
    # the plain build command honest instead of leaving a failure that has to be explained
    # away, and costs nothing on platforms where they did compile, since nothing links them.
    foreach(unused_bimg_codec bimg_decode bimg_encode)
        if(TARGET ${unused_bimg_codec})
            set_target_properties(${unused_bimg_codec} PROPERTIES EXCLUDE_FROM_ALL TRUE)
        endif()
    endforeach()

    if(TINA_NEEDS_BGFX_SHADERC AND MSVC AND TARGET shaderc)
        target_compile_options(shaderc PRIVATE /MP4)
    endif()
endif()

# EASTL/EABase retired with Legacy product (no remaining consumers).
