# Included by a per-product, per-configuration generated script. Both passes are
# static PE inspection; neither executes the product or its libraries.
function(tina_collect_runtime_dependencies output)
    file(GET_RUNTIME_DEPENDENCIES
        RESOLVED_DEPENDENCIES_VAR _resolved
        UNRESOLVED_DEPENDENCIES_VAR _unresolved
        CONFLICTING_DEPENDENCIES_PREFIX _conflicting
        ${ARGN}
        PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
        POST_EXCLUDE_REGEXES ".*[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\\\].*")
    if(_unresolved OR _conflicting_FILENAMES)
        message(FATAL_ERROR
            "Incomplete runtime dependencies for ${TINA_RUNTIME_EXECUTABLE}: "
            "unresolved=[${_unresolved}], conflicting=[${_conflicting_FILENAMES}]")
    endif()
    set(${output} "${_resolved}" PARENT_SCOPE)
endfunction()

function(tina_copy_runtime_contents destination)
    file(MAKE_DIRECTORY "${destination}")
    foreach(_dll IN LISTS ARGN)
        get_filename_component(_name "${_dll}" NAME)
        if(NOT _dll STREQUAL "${destination}/${_name}")
            file(COPY_FILE "${_dll}" "${destination}/${_name}" ONLY_IF_DIFFERENT)
        endif()
    endforeach()
endfunction()

if(NOT EXISTS "${TINA_RUNTIME_EXECUTABLE}")
    message(FATAL_ERROR "Product executable does not exist: ${TINA_RUNTIME_EXECUTABLE}")
endif()
get_filename_component(_tina_executable_directory "${TINA_RUNTIME_EXECUTABLE}" DIRECTORY)
if(NOT DEFINED TINA_RUNTIME_DESTINATION)
    set(TINA_RUNTIME_DESTINATION "${_tina_executable_directory}")
endif()
set(_tina_runtime_directories "")
foreach(_tina_dll IN LISTS TINA_RUNTIME_DLLS)
    get_filename_component(_tina_dll_directory "${_tina_dll}" DIRECTORY)
    list(APPEND _tina_runtime_directories "${_tina_dll_directory}")
endforeach()
list(REMOVE_DUPLICATES _tina_runtime_directories)

set(_tina_source_dependencies ${TINA_RUNTIME_DLLS})
if(TINA_RUNTIME_DLLS)
    # Start at the selected configuration's original libraries, not old app-local
    # copies. Otherwise an existing PNG/zlib DLL can shadow the correct package.
    tina_collect_runtime_dependencies(_tina_transitive_dependencies
        LIBRARIES ${TINA_RUNTIME_DLLS}
        DIRECTORIES ${_tina_runtime_directories})
    list(APPEND _tina_source_dependencies ${_tina_transitive_dependencies})
    list(REMOVE_DUPLICATES _tina_source_dependencies)
    tina_copy_runtime_contents("${_tina_executable_directory}" ${_tina_source_dependencies})
endif()

# Inspect the refreshed executable separately. Mixing source and staged roots in
# one walk would flag identical copies at different paths as conflicts.
tina_collect_runtime_dependencies(_tina_executable_dependencies
    EXECUTABLES "${TINA_RUNTIME_EXECUTABLE}"
    DIRECTORIES "${_tina_executable_directory}" ${_tina_runtime_directories})

# Prefer source files and emit each destination filename once. A Windows loader
# directory cannot satisfy two different DLLs of the same name.
set(_tina_packaged_dependencies "")
set(_tina_packaged_names "")
foreach(_tina_dll IN LISTS _tina_source_dependencies _tina_executable_dependencies)
    get_filename_component(_tina_dll_name "${_tina_dll}" NAME)
    string(TOLOWER "${_tina_dll_name}" _tina_dll_key)
    list(FIND _tina_packaged_names "${_tina_dll_key}" _tina_existing_index)
    if(_tina_existing_index LESS 0)
        list(APPEND _tina_packaged_names "${_tina_dll_key}")
        list(APPEND _tina_packaged_dependencies "${_tina_dll}")
    else()
        list(GET _tina_packaged_dependencies ${_tina_existing_index} _tina_existing_dll)
        file(SHA256 "${_tina_existing_dll}" _tina_existing_hash)
        file(SHA256 "${_tina_dll}" _tina_candidate_hash)
        if(NOT _tina_existing_hash STREQUAL _tina_candidate_hash)
            message(FATAL_ERROR "Conflicting runtime DLL contents: ${_tina_existing_dll};${_tina_dll}")
        endif()
    endif()
endforeach()

if(_tina_packaged_dependencies)
    # Debug/Release vcpkg DLLs may have identical timestamps. Content-based copy
    # corrects stale bytes before INSTALL applies its timestamp/manifest logic.
    tina_copy_runtime_contents("$ENV{DESTDIR}${TINA_RUNTIME_DESTINATION}" ${_tina_packaged_dependencies})
    file(INSTALL DESTINATION "${TINA_RUNTIME_DESTINATION}" TYPE SHARED_LIBRARY
        FILES ${_tina_packaged_dependencies})
endif()
