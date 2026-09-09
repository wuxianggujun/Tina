include(TinaBgfxEmbeddedShaders)
_tina_resolve_bgfx_shaderc("samples/postprocess_custom" SHADERC_COMMAND SHADERC_DEPENDS)
tina_find_assetc(POSTPROCESS_ASSETC)
if(POSTPROCESS_ASSETC MATCHES "NOTFOUND")
    message(FATAL_ERROR "postprocess_custom requires a host tina_assetc")
endif()

if(NOT Tina_SHADER_INCLUDE_DIR)
    set(Tina_SHADER_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/src/render/bgfx/shaders")
endif()
if(NOT Tina_BGFX_SHADER_INCLUDE_DIR)
    set(Tina_BGFX_SHADER_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/thirdparty/bgfx.cmake/bgfx/src")
endif()

set(POSTPROCESS_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/fs_grade.sc")
set(POSTPROCESS_PAYLOAD "${CMAKE_CURRENT_BINARY_DIR}/generated/fs_post_grade.shaderpayload")
set(POSTPROCESS_CONTRACT "${Tina_SHADER_INCLUDE_DIR}/tina_postprocess.sh")
set(POSTPROCESS_VARYING "${Tina_SHADER_INCLUDE_DIR}/tina_postprocess.def.sc")
add_custom_command(
    OUTPUT "${POSTPROCESS_PAYLOAD}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "${POSTPROCESS_ASSETC}"
        --shader-source "${POSTPROCESS_SOURCE}"
        --shader-out "${POSTPROCESS_PAYLOAD}"
        --shader-kind PostProcess
        --shaderc "${SHADERC_COMMAND}"
        --shader-varying-def "${POSTPROCESS_VARYING}"
        --shader-include "${Tina_SHADER_INCLUDE_DIR}"
        --shader-include "${Tina_BGFX_SHADER_INCLUDE_DIR}"
    DEPENDS "${POSTPROCESS_ASSETC}" ${SHADERC_DEPENDS}
        "${POSTPROCESS_SOURCE}" "${POSTPROCESS_CONTRACT}" "${POSTPROCESS_VARYING}"
        "${Tina_BGFX_SHADER_INCLUDE_DIR}/bgfx_shader.sh"
    COMMENT "Cooking PostProcess fragment shader payload"
    VERBATIM
)
set_source_files_properties("${POSTPROCESS_PAYLOAD}" PROPERTIES HEADER_FILE_ONLY ON GENERATED ON)
add_custom_target(tina_sample_postprocess_custom_payload DEPENDS "${POSTPROCESS_PAYLOAD}")
if(TARGET tina_assetc)
    add_dependencies(tina_sample_postprocess_custom_payload tina_assetc)
endif()
tina_add_desktop_frontend(tina_sample_postprocess_custom
    SOURCES DesktopMain.cpp "${POSTPROCESS_PAYLOAD}"
    CONTENT tina_sample_postprocess_custom_core
)
add_dependencies(tina_sample_postprocess_custom tina_sample_postprocess_custom_payload)
add_custom_command(TARGET tina_sample_postprocess_custom POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${POSTPROCESS_PAYLOAD}"
        "$<TARGET_FILE_DIR:tina_sample_postprocess_custom>/fs_post_grade.shaderpayload"
    VERBATIM
)
