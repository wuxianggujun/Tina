# Optional FreeType UI font resolution.
# Order: CMake cache TINA_UI_FONT_PATH → env TINA_UI_FONT_PATH → optional repo fixture if present.
# Games should pass their own font bytes/catalog; the engine does not ship a CJK font pack.
#
# Two distinct uses, deliberately separate:
# - tina_target_stage_ui_font(): copies the font beside a shipped executable, which is
#   where Desktop::resolveUiFontBytes() looks. Use for anything that runs as a product.
# - tina_target_optional_ui_font(): compiles the path in. Only legitimate for build-tree
#   test fixtures that read the file directly and never ship.

set(TINA_UI_FONT_PATH "" CACHE FILEPATH
    "Optional OTF/TTF path for FreeType Desktop/sample/test fixtures (or set env TINA_UI_FONT_PATH)")
set(TINA_UI_FALLBACK_FONT_PATHS "" CACHE STRING "Ordered semicolon-separated fallback font paths")
set(TINA_UI_FONT_STRINGS "${PROJECT_SOURCE_DIR}/resources/fonts/ui-strings.txt" CACHE FILEPATH
    "UTF-8 UI string manifest; only its shaped glyphs seed the MSDF cache")
set(TINA_MSDFGEN_EXECUTABLE "" CACHE FILEPATH "Host tina_msdfgen for cross-compilation font cooking")

function(tina_stage_generated_font_file target source name)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${source}" "$<TARGET_FILE_DIR:${target}>/assets/${name}"
        VERBATIM)
    tina_product_install_dir(${target} _install_dir)
    install(FILES "${source}" DESTINATION "${_install_dir}/assets" RENAME "${name}"
        COMPONENT ${TINA_PRODUCT_COMPONENT})
endfunction()

function(tina_target_bake_ui_font target font)
    if(NOT TINA_BUILD_UI_FREETYPE)
        return()
    endif()
    if(NOT TARGET tina_ui_font_atlas)
        if(CMAKE_CROSSCOMPILING OR NOT TINA_BUILD_TOOLS)
            if(TINA_MSDFGEN_EXECUTABLE STREQUAL "" OR NOT EXISTS "${TINA_MSDFGEN_EXECUTABLE}")
                message(FATAL_ERROR "Font cooking requires a host tina_msdfgen; set TINA_MSDFGEN_EXECUTABLE")
            endif()
            set(_tool "${TINA_MSDFGEN_EXECUTABLE}")
            set(_tool_dependency "${TINA_MSDFGEN_EXECUTABLE}")
        else()
            # The host tool target is defined later in tools/, never in the
            # Desktop dependency graph (which would create a cycle).
            set(_tool "$<TARGET_FILE:tina_msdfgen>")
            set(_tool_dependency tina_msdfgen)
        endif()
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        set(_stem "${CMAKE_BINARY_DIR}/fonts/$<CONFIG>/ui-font")
        add_custom_command(OUTPUT "${_stem}.png" "${_stem}.json" "${_stem}.tmsdf"
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/fonts/bake_ui_font.py"
                --msdfgen "${_tool}" --font "${font}" --strings "${TINA_UI_FONT_STRINGS}" --output "${_stem}"
            DEPENDS ${_tool_dependency} "${font}" "${TINA_UI_FONT_STRINGS}"
                "${PROJECT_SOURCE_DIR}/tools/fonts/bake_ui_font.py"
            COMMENT "Baking UI string glyphs with HarfBuzz + msdfgen (no full charmap loading)"
            VERBATIM)
        add_custom_target(tina_ui_font_atlas DEPENDS "${_stem}.png" "${_stem}.json" "${_stem}.tmsdf")
        set_property(TARGET tina_ui_font_atlas PROPERTY TINA_FONT_STEM "${_stem}")
        set(_fallback_manifest "")
        set(_index 0)
        foreach(_fallback IN LISTS TINA_UI_FALLBACK_FONT_PATHS)
            if(NOT EXISTS "${_fallback}")
                message(FATAL_ERROR "Fallback font does not exist: ${_fallback}")
            endif()
            string(APPEND _fallback_manifest "ui-fallback-${_index}.otf\n")
            math(EXPR _index "${_index} + 1")
        endforeach()
        if(_index GREATER 7)
            message(FATAL_ERROR "Default UI font face budget allows one primary plus seven fallback fonts")
        endif()
        file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/fonts/ui-font-fallbacks.txt" CONTENT "${_fallback_manifest}")
    endif()
    add_dependencies(${target} tina_ui_font_atlas)
    get_target_property(_stem tina_ui_font_atlas TINA_FONT_STEM)
    foreach(_extension png json tmsdf)
        tina_stage_generated_font_file(${target} "${_stem}.${_extension}" "ui-font.${_extension}")
    endforeach()
    tina_stage_generated_font_file(${target} "${CMAKE_BINARY_DIR}/fonts/ui-font-fallbacks.txt" "ui-font-fallbacks.txt")
    set(_index 0)
    foreach(_fallback IN LISTS TINA_UI_FALLBACK_FONT_PATHS)
        tina_product_data_file(${target} "${_fallback}" "assets/ui-fallback-${_index}.otf")
        math(EXPR _index "${_index} + 1")
    endforeach()
endfunction()

function(tina_resolve_ui_font_path out_var)
    set(_path "${TINA_UI_FONT_PATH}")
    if(_path STREQUAL "" AND DEFINED ENV{TINA_UI_FONT_PATH})
        set(_path "$ENV{TINA_UI_FONT_PATH}")
    endif()
    if(_path STREQUAL "")
        set(_fixture "${PROJECT_SOURCE_DIR}/resources/fonts/SourceHanSansSC-Regular.otf")
        if(EXISTS "${_fixture}")
            set(_path "${_fixture}")
        endif()
    endif()
    if(NOT _path STREQUAL "" AND NOT EXISTS "${_path}")
        message(WARNING "TINA_UI_FONT_PATH does not exist: ${_path}")
        set(_path "")
    endif()
    set(${out_var} "${_path}" PARENT_SCOPE)
endfunction()

# Stages the resolved font as assets/ui-font.otf, which is
# Tina::Desktop::DefaultUiFontRelativePath. Keep the two in sync: the product resolves the
# path at runtime, so a rename here silently produces font-less text rather than an error.
#
# A copy per product is intentional. The alternative, compiling the source path into the
# binary, is what made installed games look for a font on the machine that built them.
function(tina_target_stage_ui_font target)
    tina_resolve_ui_font_path(_resolved)
    if(_resolved STREQUAL "")
        return()
    endif()
    # Goes through tina_product_data_file() so the font reaches an installed game too.
    # Staging it into the build tree alone is what made the sample look correct here
    # while every installed copy fell back to placeholder text.
    tina_product_data_file(${target} "${_resolved}" "assets/ui-font.otf")
    tina_target_bake_ui_font(${target} "${_resolved}")
endfunction()

# Adds PRIVATE compile definitions when a font path is available:
#   TINA_UI_FONT_PATH="..."  (always when resolved)
# plus any extra names passed as ARGN.
#
# Build-tree fixtures only. A shipped target wants tina_target_stage_ui_font() instead.
function(tina_target_optional_ui_font target)
    tina_resolve_ui_font_path(_resolved)
    if(_resolved STREQUAL "")
        return()
    endif()
    # CMake needs escaped quotes inside the definition value for MSVC string macros.
    set(_defs "TINA_UI_FONT_PATH=\"${_resolved}\"")
    foreach(_name IN LISTS ARGN)
        list(APPEND _defs "${_name}=\"${_resolved}\"")
    endforeach()
    target_compile_definitions(${target} PRIVATE ${_defs})
endfunction()
