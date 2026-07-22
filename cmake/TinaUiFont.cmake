# Optional FreeType UI font resolution.
# Order: CMake cache TINA_UI_FONT_PATH → env TINA_UI_FONT_PATH → optional repo fixture if present.
# Games should pass their own font bytes/catalog; the engine does not ship a CJK font pack.

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

# Adds PRIVATE compile definitions when a font path is available:
#   TINA_UI_FONT_PATH="..."  (always when resolved)
# plus any extra names passed as ARGN (e.g. TINA_DESKTOP_UI_FONT_PATH).
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
