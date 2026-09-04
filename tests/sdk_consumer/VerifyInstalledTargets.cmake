function(tina_verify_installed_targets)
    set(tina_targets ${ARGN})
    list(REMOVE_DUPLICATES tina_targets)

    foreach(tina_target IN LISTS tina_targets)
        if(NOT TARGET ${tina_target})
            message(FATAL_ERROR "The Tina package is missing declared target ${tina_target}")
        endif()

        get_target_property(tina_target_includes ${tina_target} INTERFACE_INCLUDE_DIRECTORIES)
        if(NOT tina_target_includes OR tina_target_includes MATCHES "-NOTFOUND$")
            continue()
        endif()
        foreach(tina_include IN LISTS tina_target_includes)
            if(DEFINED TINA_FORBIDDEN_SOURCE_DIR AND NOT TINA_FORBIDDEN_SOURCE_DIR STREQUAL "")
                cmake_path(IS_PREFIX TINA_FORBIDDEN_SOURCE_DIR "${tina_include}" NORMALIZE tina_uses_source_include)
                if(tina_uses_source_include)
                    message(FATAL_ERROR "${tina_target} leaks source-tree include directory ${tina_include}")
                endif()
            endif()
            if(DEFINED TINA_EXPECTED_INSTALL_PREFIX AND NOT TINA_EXPECTED_INSTALL_PREFIX STREQUAL "")
                cmake_path(IS_PREFIX TINA_EXPECTED_INSTALL_PREFIX "${tina_include}" NORMALIZE tina_uses_install_prefix)
                if(NOT tina_uses_install_prefix)
                    message(FATAL_ERROR
                        "${tina_target} uses include directory outside the install prefix: ${tina_include}")
                endif()
            endif()
        endforeach()
    endforeach()
endfunction()

# The shader authoring inputs are plain files rather than targets, so target verification above
# cannot see them. They are also the one part of the package a source-tree build cannot exercise:
# samples/{2d,3d}_custom_shader fall back to ${PROJECT_SOURCE_DIR}/src/render/bgfx/shaders when
# these variables are unset, so an in-tree build succeeds whether or not the package installs a
# single one of these files -- which is how the package shipped a cooker with none of its inputs.
#
# Both variables track whether the package *shipped* the bgfx renderer, not whether this consumer
# asked for the RenderBgfx component: TinaConfig sets them unconditionally, while
# Tina_RenderBgfx_FOUND additionally requires the component to be requested. A GameSDK-only consumer
# of a bgfx-enabled package therefore sees populated shader dirs and RenderBgfx_FOUND false, so the
# caller states which it expects rather than the function guessing from component state.
function(tina_verify_installed_shader_include_dirs tina_expect_shader_inputs)
    if(NOT tina_expect_shader_inputs)
        foreach(tina_shader_var IN ITEMS Tina_SHADER_INCLUDE_DIR Tina_BGFX_SHADER_INCLUDE_DIR)
            if(NOT "${${tina_shader_var}}" STREQUAL "")
                message(FATAL_ERROR
                    "${tina_shader_var} is '${${tina_shader_var}}' in a package without the bgfx renderer")
            endif()
        endforeach()
        return()
    endif()

    foreach(tina_shader_var IN ITEMS Tina_SHADER_INCLUDE_DIR Tina_BGFX_SHADER_INCLUDE_DIR)
        if("${${tina_shader_var}}" STREQUAL "")
            message(FATAL_ERROR "${tina_shader_var} is empty in a package that ships the bgfx renderer")
        endif()
        # set_and_check only proves the directory exists, which a leaked source-tree path also does.
        if(DEFINED TINA_EXPECTED_INSTALL_PREFIX AND NOT TINA_EXPECTED_INSTALL_PREFIX STREQUAL "")
            cmake_path(IS_PREFIX TINA_EXPECTED_INSTALL_PREFIX "${${tina_shader_var}}" NORMALIZE
                tina_shader_dir_inside_prefix)
            if(NOT tina_shader_dir_inside_prefix)
                message(FATAL_ERROR
                    "${tina_shader_var} points outside the install prefix: ${${tina_shader_var}}")
            endif()
        endif()
    endforeach()

    # Exactly what a documented custom fragment shader cook reads: the contract each source must
    # include, and the varying def that fixes its stage interface.
    foreach(tina_shader_input IN ITEMS
            "tina_sprite2d.sh"
            "tina_mesh3d.sh"
            "tina_sprite2d_fixture.def.sc"
            "tina_opaque3d_mr.def.sc")
        if(NOT EXISTS "${Tina_SHADER_INCLUDE_DIR}/${tina_shader_input}")
            message(FATAL_ERROR
                "Tina_SHADER_INCLUDE_DIR is missing shader authoring input ${tina_shader_input}")
        endif()
    endforeach()

    # Both Tina contracts #include this, so a cook fails without it even though no consumer names it.
    if(NOT EXISTS "${Tina_BGFX_SHADER_INCLUDE_DIR}/bgfx_shader.sh")
        message(FATAL_ERROR "Tina_BGFX_SHADER_INCLUDE_DIR is missing bgfx_shader.sh")
    endif()
endfunction()

function(tina_verify_backend_neutral_game_sdk)
    if(NOT TARGET Tina::GameSDK)
        message(FATAL_ERROR "The Tina package does not provide Tina::GameSDK")
    endif()

    get_target_property(tina_game_sdk_links Tina::GameSDK INTERFACE_LINK_LIBRARIES)
    set(tina_forbidden_game_sdk_targets
        Tina::PlatformGlfw
        Tina::RenderBgfx
        Tina::UIFreetype
        Tina::AudioMiniaudio
        Tina::DesktopBootstrap
        ${Tina_ADAPTER_TARGETS}
    )
    list(REMOVE_DUPLICATES tina_forbidden_game_sdk_targets)
    foreach(tina_adapter IN LISTS tina_forbidden_game_sdk_targets)
        if(tina_adapter IN_LIST tina_game_sdk_links)
            message(FATAL_ERROR "Tina::GameSDK unexpectedly links backend adapter ${tina_adapter}")
        endif()
    endforeach()
endfunction()
