include_guard(GLOBAL)

include(GNUInstallDirs)

# A shipped product is its executable plus the files that executable resolves
# relative to itself. The build tree and the install tree have to lay those out
# identically, because Core::applicationFilePath() is the only lookup either one
# gets: a layout that holds in one and not the other yields a game that runs on the
# machine it was built on and nowhere else.
#
# So the two halves are never written apart. tina_product_data_file() emits the
# build-tree copy and the install rule from one relative path, and
# tina_install_product() puts the executable at the root those paths are relative
# to. Writing one half by hand is how the UI font came to be staged beside the
# sample yet absent from every install.

# One directory per product, so that directory is the unit you copy to another
# machine. The component name lets a game be installed without the SDK's headers
# and import libraries.
set(TINA_PRODUCT_COMPONENT "products")

# Named after the executable as shipped, not after the CMake target, so the directory a
# user copies matches the program inside it: tina_editor_desktop ships as TinaEditor.
function(tina_product_install_dir target out_var)
    get_target_property(_output_name ${target} OUTPUT_NAME)
    if(_output_name)
        set(${out_var} "${_output_name}" PARENT_SCOPE)
    else()
        set(${out_var} "${target}" PARENT_SCOPE)
    endif()
endfunction()

# Stages `source` at `relative_destination` under the target's output directory and
# installs it at the same relative path. `relative_destination` is the string the
# product hands to Core::applicationFilePath(), file name included: it is allowed to
# differ from the source's own name, which is how a font fixture becomes ui-font.otf.
function(tina_product_data_file target source relative_destination)
    if(NOT EXISTS "${source}")
        message(FATAL_ERROR
            "tina_product_data_file(${target}): source does not exist: ${source}")
    endif()
    get_filename_component(_file_name "${relative_destination}" NAME)
    if(_file_name STREQUAL "")
        message(FATAL_ERROR
            "tina_product_data_file(${target}): destination names no file: ${relative_destination}")
    endif()
    get_filename_component(_relative_dir "${relative_destination}" DIRECTORY)

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${source}"
            "$<TARGET_FILE_DIR:${target}>/${relative_destination}"
        COMMENT "Staging ${relative_destination} for ${target}"
        VERBATIM
    )

    tina_product_install_dir(${target} _install_dir)
    if(NOT _relative_dir STREQUAL "")
        set(_install_dir "${_install_dir}/${_relative_dir}")
    endif()
    install(FILES "${source}"
        DESTINATION "${_install_dir}"
        RENAME "${_file_name}"
        COMPONENT ${TINA_PRODUCT_COMPONENT}
    )
endfunction()

# Installs the executable, and on Windows the DLLs it loads, into the product's own
# directory. Those DLLs belong beside the executable rather than in a library
# directory because that is where the loader looks, which is the same reason the
# build tree copies them next to it.
function(tina_install_product target)
    tina_product_install_dir(${target} _install_dir)
    install(TARGETS ${target}
        RUNTIME DESTINATION "${_install_dir}"
        COMPONENT ${TINA_PRODUCT_COMPONENT}
    )
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target}>
                $<TARGET_FILE_DIR:${target}>
            COMMAND_EXPAND_LISTS
        )
        install(FILES $<TARGET_RUNTIME_DLLS:${target}>
            DESTINATION "${_install_dir}"
            COMPONENT ${TINA_PRODUCT_COMPONENT}
        )
    endif()
endfunction()
