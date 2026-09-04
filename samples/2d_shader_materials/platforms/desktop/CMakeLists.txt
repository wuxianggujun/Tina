# Cooks assets/fs_tint.sc into a Shader payload at build time and stages it beside the executable.
#
# Same shape as samples/2d_custom_shader: the cook belongs to the platform frontend because it
# produces this platform's shipped file, while core/ only asks Core::applicationFilePath for the
# payload by name.
include(TinaBgfxEmbeddedShaders)
_tina_resolve_bgfx_shaderc("samples/2d_shader_materials" SHADERC_COMMAND SHADERC_DEPENDS)

tina_find_assetc(TINA_SHADER_MATERIALS_ASSETC)
if(TINA_SHADER_MATERIALS_ASSETC MATCHES "NOTFOUND")
    message(FATAL_ERROR
        "samples/2d_shader_materials needs the asset cooker to cook its fragment shader, and none "
        "was found (${TINA_SHADER_MATERIALS_ASSETC}). Set TINA_ASSETC_EXECUTABLE to a host build of "
        "tina_assetc.")
endif()

set(TINT_SHADER_PAYLOAD "${CMAKE_CURRENT_BINARY_DIR}/generated/fs_tint.shaderpayload")
set(TINT_SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/fs_tint.sc")
set(TINT_SHADER_CONTRACT "${PROJECT_SOURCE_DIR}/src/render/bgfx/shaders/tina_sprite2d.sh")
set(TINT_VARYING_DEF "${PROJECT_SOURCE_DIR}/src/render/bgfx/shaders/tina_sprite2d_fixture.def.sc")

add_custom_command(
    OUTPUT "${TINT_SHADER_PAYLOAD}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "${TINA_SHADER_MATERIALS_ASSETC}"
        --shader-source "${TINT_SHADER_SOURCE}"
        --shader-out "${TINT_SHADER_PAYLOAD}"
        --shader-kind Sprite2D
        --shaderc "${SHADERC_COMMAND}"
        --shader-varying-def "${TINT_VARYING_DEF}"
        --shader-include "${PROJECT_SOURCE_DIR}/src/render/bgfx/shaders"
        --shader-include "${PROJECT_SOURCE_DIR}/thirdparty/bgfx.cmake/bgfx/src"
    DEPENDS
        ${SHADERC_DEPENDS}
        "${TINT_SHADER_SOURCE}"
        "${TINT_SHADER_CONTRACT}"
        "${TINT_VARYING_DEF}"
    COMMENT "Cooking 2d_shader_materials fragment shader payload"
    VERBATIM
)
if(TARGET tina_assetc)
    add_custom_target(tina_sample_2d_shader_materials_payload DEPENDS "${TINT_SHADER_PAYLOAD}")
    add_dependencies(tina_sample_2d_shader_materials_payload tina_assetc)
endif()

tina_add_desktop_frontend(tina_sample_2d_shader_materials
    SOURCES
        DesktopMain.cpp
        "${TINT_SHADER_PAYLOAD}"
    CONTENT tina_sample_2d_shader_materials_core
)
set_source_files_properties("${TINT_SHADER_PAYLOAD}" PROPERTIES HEADER_FILE_ONLY ON GENERATED ON)
if(TARGET tina_sample_2d_shader_materials_payload)
    add_dependencies(tina_sample_2d_shader_materials tina_sample_2d_shader_materials_payload)
endif()

add_custom_command(TARGET tina_sample_2d_shader_materials POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${TINT_SHADER_PAYLOAD}"
        "$<TARGET_FILE_DIR:tina_sample_2d_shader_materials>/fs_tint.shaderpayload"
    VERBATIM
)
