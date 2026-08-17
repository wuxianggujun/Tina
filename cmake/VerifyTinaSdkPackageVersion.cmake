if(NOT DEFINED TINA_SDK_VERSION_FILE OR TINA_SDK_VERSION_FILE STREQUAL "")
    message(FATAL_ERROR
        "TINA_SDK_VERSION_FILE must name a generated TinaConfigVersion.cmake")
endif()

cmake_path(ABSOLUTE_PATH TINA_SDK_VERSION_FILE NORMALIZE
    OUTPUT_VARIABLE tina_sdk_version_file)
if(NOT EXISTS "${tina_sdk_version_file}")
    message(FATAL_ERROR
        "Tina SDK package version file does not exist: ${tina_sdk_version_file}")
endif()
if(IS_DIRECTORY "${tina_sdk_version_file}")
    message(FATAL_ERROR
        "TINA_SDK_VERSION_FILE must name a file, not a directory: ${tina_sdk_version_file}")
endif()

function(tina_verify_version_request requested_version requested_range
         expected_compatible expected_exact)
    string(REPLACE "." ";" requested_components "${requested_version}")
    list(LENGTH requested_components requested_count)
    if(requested_count LESS 1 OR requested_count GREATER 4)
        message(FATAL_ERROR "Invalid Tina SDK version probe: ${requested_version}")
    endif()
    foreach(requested_component IN LISTS requested_components)
        if(NOT requested_component MATCHES "^[0-9]+$")
            message(FATAL_ERROR "Invalid Tina SDK version probe: ${requested_version}")
        endif()
    endforeach()

    set(requested_major 0)
    set(requested_minor 0)
    set(requested_patch 0)
    set(requested_tweak 0)
    list(GET requested_components 0 requested_major)
    if(requested_count GREATER 1)
        list(GET requested_components 1 requested_minor)
    endif()
    if(requested_count GREATER 2)
        list(GET requested_components 2 requested_patch)
    endif()
    if(requested_count GREATER 3)
        list(GET requested_components 3 requested_tweak)
    endif()

    # Reproduce the scalar variables set by find_package() before it includes a
    # ConfigVersion file. Clear every output so one probe cannot inherit a true
    # result from the preceding request.
    set(PACKAGE_FIND_VERSION "${requested_version}")
    set(PACKAGE_FIND_VERSION_MAJOR "${requested_major}")
    set(PACKAGE_FIND_VERSION_MINOR "${requested_minor}")
    set(PACKAGE_FIND_VERSION_PATCH "${requested_patch}")
    set(PACKAGE_FIND_VERSION_TWEAK "${requested_tweak}")
    set(PACKAGE_FIND_VERSION_COUNT "${requested_count}")
    set(PACKAGE_FIND_VERSION_RANGE "${requested_range}")
    unset(PACKAGE_VERSION)
    unset(PACKAGE_VERSION_COMPATIBLE)
    unset(PACKAGE_VERSION_EXACT)
    unset(PACKAGE_VERSION_UNSUITABLE)

    include("${tina_sdk_version_file}")

    set(actual_compatible FALSE)
    if(DEFINED PACKAGE_VERSION_COMPATIBLE AND PACKAGE_VERSION_COMPATIBLE)
        set(actual_compatible TRUE)
    endif()
    set(actual_exact FALSE)
    if(DEFINED PACKAGE_VERSION_EXACT AND PACKAGE_VERSION_EXACT)
        set(actual_exact TRUE)
    endif()

    if(NOT actual_compatible STREQUAL expected_compatible OR
       NOT actual_exact STREQUAL expected_exact)
        message(FATAL_ERROR
            "Tina SDK version request ${requested_version} produced "
            "compatible=${actual_compatible}, exact=${actual_exact}; expected "
            "compatible=${expected_compatible}, exact=${expected_exact}")
    endif()
endfunction()

tina_verify_version_request("0.0.1" "" TRUE TRUE)
tina_verify_version_request("0.0.0" "" FALSE FALSE)
tina_verify_version_request("0.0.2" "" FALSE FALSE)
tina_verify_version_request("0.0.1.0" "" FALSE FALSE)
tina_verify_version_request("0.0.1" "0.0.1...<0.0.2" FALSE FALSE)

message(STATUS
    "Verified Tina SDK strict exact-version policy: 0.0.1 exact; adjacent, tweak, and range requests rejected")
