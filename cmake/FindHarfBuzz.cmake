include(FindPackageHandleStandardArgs)
include(SelectLibraryConfigurations)

# The pinned port's harfbuzzConfig.cmake resolves libraries through private vcpkg
# variables rather than its own prefix. A CMAKE_PREFIX_PATH-only SDK consumer has
# no such variables, and receives a NOTFOUND imported location without a configure
# error. Discover the public headers/libraries directly, as we do for FriBidi.
find_path(HarfBuzz_INCLUDE_DIR NAMES hb.h PATH_SUFFIXES harfbuzz)
if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
    set(_harfbuzz_prefix "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    find_library(HarfBuzz_LIBRARY_RELEASE NAMES harfbuzz
        PATHS "${_harfbuzz_prefix}/lib" NO_DEFAULT_PATH)
    find_library(HarfBuzz_LIBRARY_DEBUG NAMES harfbuzz
        PATHS "${_harfbuzz_prefix}/debug/lib" NO_DEFAULT_PATH)
else()
    find_library(HarfBuzz_LIBRARY_RELEASE NAMES harfbuzz)
    if(HarfBuzz_LIBRARY_RELEASE)
        get_filename_component(_harfbuzz_libdir "${HarfBuzz_LIBRARY_RELEASE}" DIRECTORY)
        find_library(HarfBuzz_LIBRARY_DEBUG NAMES harfbuzz harfbuzzd
            PATHS "${_harfbuzz_libdir}/../debug/lib" NO_DEFAULT_PATH)
    endif()
    find_library(HarfBuzz_LIBRARY_DEBUG NAMES harfbuzzd)
endif()
select_library_configurations(HarfBuzz)
find_package_handle_standard_args(HarfBuzz REQUIRED_VARS HarfBuzz_LIBRARY HarfBuzz_INCLUDE_DIR)

if(HarfBuzz_FOUND AND NOT TARGET harfbuzz::harfbuzz)
    if(WIN32)
        foreach(_configuration RELEASE DEBUG)
            if(HarfBuzz_LIBRARY_${_configuration})
                get_filename_component(_harfbuzz_libdir "${HarfBuzz_LIBRARY_${_configuration}}" DIRECTORY)
                find_file(HarfBuzz_DLL_${_configuration} NAMES harfbuzz.dll
                    PATHS "${_harfbuzz_libdir}/../bin" NO_DEFAULT_PATH)
            endif()
        endforeach()
    endif()
    if(HarfBuzz_DLL_RELEASE OR HarfBuzz_DLL_DEBUG)
        add_library(harfbuzz::harfbuzz SHARED IMPORTED)
    else()
        add_library(harfbuzz::harfbuzz UNKNOWN IMPORTED)
    endif()
    set_target_properties(harfbuzz::harfbuzz PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${HarfBuzz_INCLUDE_DIR}"
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
        MAP_IMPORTED_CONFIG_MINSIZEREL RELEASE)
    foreach(_configuration RELEASE DEBUG)
        if(HarfBuzz_LIBRARY_${_configuration})
            set_property(TARGET harfbuzz::harfbuzz APPEND PROPERTY IMPORTED_CONFIGURATIONS ${_configuration})
            if(HarfBuzz_DLL_${_configuration})
                set_target_properties(harfbuzz::harfbuzz PROPERTIES
                    IMPORTED_IMPLIB_${_configuration} "${HarfBuzz_LIBRARY_${_configuration}}"
                    IMPORTED_LOCATION_${_configuration} "${HarfBuzz_DLL_${_configuration}}")
            else()
                set_target_properties(harfbuzz::harfbuzz PROPERTIES
                    IMPORTED_LOCATION_${_configuration} "${HarfBuzz_LIBRARY_${_configuration}}")
            endif()
        endif()
    endforeach()
    # The pinned core build includes hb-ft; retain its dependency for static links.
    if(TARGET Freetype::Freetype)
        set_property(TARGET harfbuzz::harfbuzz APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES Freetype::Freetype)
    endif()
endif()
mark_as_advanced(HarfBuzz_INCLUDE_DIR HarfBuzz_LIBRARY_RELEASE HarfBuzz_LIBRARY_DEBUG
    HarfBuzz_DLL_RELEASE HarfBuzz_DLL_DEBUG)
