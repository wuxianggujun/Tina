include_guard(GLOBAL)

function(tina_collect_test_targets directory output)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    set(tests)
    foreach(target IN LISTS targets)
        get_target_property(kind ${target} TYPE)
        if(kind STREQUAL "EXECUTABLE" AND target MATCHES "^tina_.*tests$")
            list(APPEND tests "${target}")
        endif()
    endforeach()
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        tina_collect_test_targets("${child}" child_tests)
        list(APPEND tests ${child_tests})
    endforeach()
    set(${output} "${tests}" PARENT_SCOPE)
endfunction()

function(tina_configure_validation_targets)
    # A source-policy gate, not a runtime test. Only the validation graph needs
    # Python; SDK consumers and TINA_BUILD_TESTING=OFF do not acquire this dependency.
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    add_custom_target(tina_core_type_check
        COMMAND "${Python3_EXECUTABLE}" -B
            "${PROJECT_SOURCE_DIR}/tools/validation/check_core_types.py"
            --root "${PROJECT_SOURCE_DIR}"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Checking first-party Core scalar type policy"
        VERBATIM)

    tina_collect_test_targets("${PROJECT_SOURCE_DIR}/tests" tests)
    if(TINA_BUILD_EDITOR)
        tina_collect_test_targets("${PROJECT_SOURCE_DIR}/editor" editor_tests)
        list(APPEND tests ${editor_tests})
    endif()
    list(REMOVE_DUPLICATES tests)
    list(SORT tests)
    set(entries)
    foreach(target IN LISTS tests)
        list(APPEND entries "{\"target\":\"${target}\",\"path\":\"$<TARGET_FILE:${target}>\"}")
    endforeach()
    string(JOIN ",\n" test_json ${entries})
    file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/tina-validation-$<CONFIG>.json"
        CONTENT "{\"schemaVersion\":1,\"buildId\":\"${TINA_SDK_BUILD_ID}\",\"configuration\":\"$<CONFIG>\",\"tests\":[\n${test_json}\n]}\n")

    add_custom_target(tina_validation_artifacts)
    add_dependencies(tina_validation_artifacts tina_core_type_check tina_sdk_install_artifacts ${tests})
    foreach(product IN ITEMS tina_editor_desktop tina_sample_2d tina_sample_3d
            tina_sample_postprocess_custom tina_sample_3d_authored_level)
        if(TARGET ${product})
            add_dependencies(tina_validation_artifacts ${product})
        endif()
    endforeach()
endfunction()
