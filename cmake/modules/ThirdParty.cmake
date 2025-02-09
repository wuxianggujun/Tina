function(tina_add_third_party)
    set(options)
    set(oneValueArgs NAME PATH BUILD_DIR)
    set(multiValueArgs OPTIONS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT ARG_BUILD_DIR)
        set(ARG_BUILD_DIR "${ARG_NAME}_build")
    endif ()

    message(STATUS "Adding third party: ${ARG_NAME} from ${ARG_PATH} to ${ARG_BUILD_DIR}")

    # 设置选项
    foreach(var IN LISTS ARG_OPTIONS)
        string(REGEX MATCH "([^=]+)=(.+)" _ ${var})
        set(name ${CMAKE_MATCH_1})
        set(value ${CMAKE_MATCH_2})
        set(${name} ${value} CACHE INTERNAL "Third party option")
    endforeach ()

    # 添加子目录
    add_subdirectory(${ARG_PATH} ${ARG_BUILD_DIR})
endfunction()