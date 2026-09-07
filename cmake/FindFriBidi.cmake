include(FindPackageHandleStandardArgs)
include(SelectLibraryConfigurations)

find_path(FriBidi_INCLUDE_DIR NAMES fribidi.h PATH_SUFFIXES fribidi)
if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
    # vcpkg prepends debug prefixes for multi-config generators. Searching the
    # general prefix list for RELEASE can silently select the Debug import lib.
    set(_fribidi_prefix "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    find_library(FriBidi_LIBRARY_RELEASE NAMES fribidi libfribidi
        PATHS "${_fribidi_prefix}/lib" NO_DEFAULT_PATH)
    find_library(FriBidi_LIBRARY_DEBUG NAMES fribidi libfribidi
        PATHS "${_fribidi_prefix}/debug/lib" NO_DEFAULT_PATH)
else()
    find_library(FriBidi_LIBRARY_RELEASE NAMES fribidi libfribidi)
    find_library(FriBidi_LIBRARY_DEBUG NAMES fribidid libfribidid)
endif()
select_library_configurations(FriBidi)
find_package_handle_standard_args(FriBidi REQUIRED_VARS FriBidi_LIBRARY FriBidi_INCLUDE_DIR)
if(FriBidi_FOUND AND NOT TARGET FriBidi::FriBidi)
    if(WIN32)
        get_filename_component(_fribidi_release_libdir "${FriBidi_LIBRARY_RELEASE}" DIRECTORY)
        get_filename_component(_fribidi_debug_libdir "${FriBidi_LIBRARY_DEBUG}" DIRECTORY)
        find_file(FriBidi_DLL_RELEASE NAMES fribidi-0.dll libfribidi-0.dll fribidi.dll
            PATHS "${_fribidi_release_libdir}/../bin" NO_DEFAULT_PATH)
        find_file(FriBidi_DLL_DEBUG NAMES fribidi-0.dll libfribidi-0.dll fribidi.dll
            PATHS "${_fribidi_debug_libdir}/../bin" NO_DEFAULT_PATH)
    endif()
    if(FriBidi_DLL_RELEASE OR FriBidi_DLL_DEBUG)
        add_library(FriBidi::FriBidi SHARED IMPORTED)
    else()
        add_library(FriBidi::FriBidi UNKNOWN IMPORTED)
    endif()
    set_target_properties(FriBidi::FriBidi PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${FriBidi_INCLUDE_DIR}"
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
        MAP_IMPORTED_CONFIG_MINSIZEREL RELEASE)
    if(FriBidi_LIBRARY_RELEASE)
        set_property(TARGET FriBidi::FriBidi APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
        if(FriBidi_DLL_RELEASE)
            set_target_properties(FriBidi::FriBidi PROPERTIES
                IMPORTED_IMPLIB_RELEASE "${FriBidi_LIBRARY_RELEASE}" IMPORTED_LOCATION_RELEASE "${FriBidi_DLL_RELEASE}")
        else()
            set_target_properties(FriBidi::FriBidi PROPERTIES IMPORTED_LOCATION_RELEASE "${FriBidi_LIBRARY_RELEASE}")
        endif()
    endif()
    if(FriBidi_LIBRARY_DEBUG)
        set_property(TARGET FriBidi::FriBidi APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
        if(FriBidi_DLL_DEBUG)
            set_target_properties(FriBidi::FriBidi PROPERTIES
                IMPORTED_IMPLIB_DEBUG "${FriBidi_LIBRARY_DEBUG}" IMPORTED_LOCATION_DEBUG "${FriBidi_DLL_DEBUG}")
        else()
            set_target_properties(FriBidi::FriBidi PROPERTIES IMPORTED_LOCATION_DEBUG "${FriBidi_LIBRARY_DEBUG}")
        endif()
    endif()
endif()
mark_as_advanced(FriBidi_INCLUDE_DIR FriBidi_LIBRARY_RELEASE FriBidi_LIBRARY_DEBUG)
