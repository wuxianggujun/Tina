# Cooks assets/fs_tinted.sc into a Mesh3D Shader payload at build time and stages it
# beside the executable.
#
# The cook lives in the desktop frontend rather than in core/ because it is what produces this
# platform's shipped file: core/ only asks Core::applicationFilePath for the payload by name, so an
# Android or web frontend would stage the same payload its own way with no change to core.
#
# tina_assetc rather than shaderc directly: the runtime parses a Shader *payload* (one blob per
# renderer profile, canonically ordered), and shaderc alone emits single-profile .bin files that the
# payload parser cannot read.
#
# _tina_resolve_bgfx_shaderc is the same resolver the engine's own embedded shaders use, so a cross
# build with -DTINA_BGFX_SHADERC_EXECUTABLE gets a host binary instead of a target one.
include(TinaBgfxEmbeddedShaders)
_tina_resolve_bgfx_shaderc("samples/3d_custom_shader" SHADERC_COMMAND SHADERC_DEPENDS)

tina_find_assetc(TINA_3D_CUSTOM_SHADER_ASSETC)
if(TINA_3D_CUSTOM_SHADER_ASSETC MATCHES "NOTFOUND")
    message(FATAL_ERROR
        "samples/3d_custom_shader needs the asset cooker to cook its fragment shader, and none "
        "was found (${TINA_3D_CUSTOM_SHADER_ASSETC}). Set TINA_ASSETC_EXECUTABLE to a host build of "
        "tina_assetc.")
endif()

set(TINTED_SHADER_PAYLOAD "${CMAKE_CURRENT_BINARY_DIR}/generated/fs_tinted.shaderpayload")
set(TINTED_SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/fs_tinted.sc")

# The two include roots the cook needs, resolved the same way an installed consumer resolves them.
# find_package(Tina) sets these; in-tree they are unset, so they fall back to the source paths. Going
# through the same variables is what keeps this recipe honest: hardcoding the in-tree paths is exactly
# how the SDK came to ship a cooker with none of its inputs.
if(NOT Tina_SHADER_INCLUDE_DIR)
    set(Tina_SHADER_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/src/render/bgfx/shaders")
endif()
if(NOT Tina_BGFX_SHADER_INCLUDE_DIR)
    set(Tina_BGFX_SHADER_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/thirdparty/bgfx.cmake/bgfx/src")
endif()
set(TINTED_SHADER_CONTRACT "${Tina_SHADER_INCLUDE_DIR}/tina_mesh3d.sh")
# One cooked fragment links against both the rigid and skinned engine vertex stages because the two
# .def.sc files declare the same varyings. The rigid def is the cook input; the skinned def is not
# needed at cook time.
set(TINTED_VARYING_DEF "${Tina_SHADER_INCLUDE_DIR}/tina_opaque3d_mr.def.sc")

add_custom_command(
    OUTPUT "${TINTED_SHADER_PAYLOAD}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "${TINA_3D_CUSTOM_SHADER_ASSETC}"
        --shader-source "${TINTED_SHADER_SOURCE}"
        --shader-out "${TINTED_SHADER_PAYLOAD}"
        --shader-kind Mesh3D
        --shaderc "${SHADERC_COMMAND}"
        --shader-varying-def "${TINTED_VARYING_DEF}"
        --shader-include "${Tina_SHADER_INCLUDE_DIR}"
        --shader-include "${Tina_BGFX_SHADER_INCLUDE_DIR}"
    DEPENDS
        ${SHADERC_DEPENDS}
        "${TINTED_SHADER_SOURCE}"
        "${TINTED_SHADER_CONTRACT}"
        "${TINTED_VARYING_DEF}"
    COMMENT "Cooking 3d_custom_shader fragment shader payload"
    VERBATIM
)
if(TARGET tina_assetc)
    # An in-tree cooker has to be built before the cook runs; an SDK/override one already exists,
    # and naming a non-target in DEPENDS would fail at generate time.
    add_custom_target(tina_sample_3d_custom_shader_payload DEPENDS "${TINTED_SHADER_PAYLOAD}")
    add_dependencies(tina_sample_3d_custom_shader_payload tina_assetc)
endif()

tina_add_desktop_frontend(tina_sample_3d_custom_shader
    SOURCES
        DesktopMain.cpp
        "${TINTED_SHADER_PAYLOAD}"
    CONTENT tina_sample_3d_custom_shader_core
)
set_source_files_properties("${TINTED_SHADER_PAYLOAD}" PROPERTIES HEADER_FILE_ONLY ON GENERATED ON)
if(TARGET tina_sample_3d_custom_shader_payload)
    add_dependencies(tina_sample_3d_custom_shader tina_sample_3d_custom_shader_payload)
endif()

# Staged beside the executable because that is the only anchor an installed program has: core/
# resolves the payload through Core::applicationFilePath, not through the build tree.
add_custom_command(TARGET tina_sample_3d_custom_shader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${TINTED_SHADER_PAYLOAD}"
        "$<TARGET_FILE_DIR:tina_sample_3d_custom_shader>/fs_tinted.shaderpayload"
    VERBATIM
)
