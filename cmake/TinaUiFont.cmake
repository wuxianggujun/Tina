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

# Stages the resolved font beside the target executable as assets/ui-font.otf, which is
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
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_resolved}"
            "$<TARGET_FILE_DIR:${target}>/assets/ui-font.otf"
    )
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
