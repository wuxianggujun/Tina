# Cooks assets/fs_lit.sc into a Shader payload at build time and stages it beside the executable.
#
# The cook lives in the desktop frontend rather than in core/ because it is what produces this
# platform's shipped file: core/ only asks Core::applicationFilePath for the payload by name.
include(TinaBgfxEmbeddedShaders)
_tina_resolve_bgfx_shaderc("samples/2d_shader_lighting" SHADERC_COMMAND SHADERC_DEPENDS)

tina_find_assetc(TINA_SHADER_LIGHTING_ASSETC)
if(TINA_SHADER_LIGHTING_ASSETC MATCHES "NOTFOUND")
    message(FATAL_ERROR
        "samples/2d_shader_lighting needs the asset cooker to cook its fragment shader, and none "
        "was found (${TINA_SHADER_LIGHTING_ASSETC}). Set TINA_ASSETC_EXECUTABLE to a host build of "
        "tina_assetc.")
endif()

set(LIT_SHADER_PAYLOAD "${CMAKE_CURRENT_BINARY_DIR}/generated/fs_lit.shaderpayload")
set(LIT_SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/fs_lit.sc")
set(LIT_SHADER_CONTRACT "${PROJECT_SOURCE_DIR}/src/render/bgfx/shaders/tina_sprite2d.sh")
set(LIT_VARYING_DEF "${PROJECT_SOURCE_DIR}/src/render/bgfx/shaders/tina_sprite2d_fixture.def.sc")

add_custom_command(
    OUTPUT "${LIT_SHADER_PAYLOAD}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "${TINA_SHADER_LIGHTING_ASSETC}"
        --shader-source "${LIT_SHADER_SOURCE}"
        --shader-out "${LIT_SHADER_PAYLOAD}"
        --shader-kind Sprite2D
        --shaderc "${SHADERC_COMMAND}"
        --shader-varying-def "${LIT_VARYING_DEF}"
        --shader-include "${PROJECT_SOURCE_DIR}/src/render/bgfx/shaders"
        --shader-include "${PROJECT_SOURCE_DIR}/thirdparty/bgfx.cmake/bgfx/src"
    DEPENDS
        ${SHADERC_DEPENDS}
        "${LIT_SHADER_SOURCE}"
        "${LIT_SHADER_CONTRACT}"
        "${LIT_VARYING_DEF}"
    COMMENT "Cooking 2d_shader_lighting fragment shader payload"
    VERBATIM
)
if(TARGET tina_assetc)
    add_custom_target(tina_sample_2d_shader_lighting_payload DEPENDS "${LIT_SHADER_PAYLOAD}")
    add_dependencies(tina_sample_2d_shader_lighting_payload tina_assetc)
endif()

tina_add_desktop_frontend(tina_sample_2d_shader_lighting
    SOURCES
        DesktopMain.cpp
        "${LIT_SHADER_PAYLOAD}"
    CONTENT tina_sample_2d_shader_lighting_core
)
set_source_files_properties("${LIT_SHADER_PAYLOAD}" PROPERTIES HEADER_FILE_ONLY ON GENERATED ON)
if(TARGET tina_sample_2d_shader_lighting_payload)
    add_dependencies(tina_sample_2d_shader_lighting tina_sample_2d_shader_lighting_payload)
endif()

add_custom_command(TARGET tina_sample_2d_shader_lighting POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${LIT_SHADER_PAYLOAD}"
        "$<TARGET_FILE_DIR:tina_sample_2d_shader_lighting>/fs_lit.shaderpayload"
    VERBATIM
)
