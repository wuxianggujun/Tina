include(ExternalProject)

function(setup_eastl)
    set(EASTL_PREFIX "${CMAKE_SOURCE_DIR}/third_party/EASTL")
    set(EABASE_SOURCE_DIR "${EASTL_PREFIX}/EABase")
    set(EASTL_SOURCE_DIR "${EASTL_PREFIX}/EASTL")

    # 解压文件
    if(NOT EXISTS "${EABASE_SOURCE_DIR}")
        message(STATUS "Extracting EABase to ${EABASE_SOURCE_DIR}")
        file(MAKE_DIRECTORY "${EABASE_SOURCE_DIR}")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xf "${CMAKE_SOURCE_DIR}/third_party/EASTL/EABase-master.zip"
            WORKING_DIRECTORY "${EASTL_PREFIX}"
        )
        file(GLOB EXTRACTED_DIR "${EASTL_PREFIX}/EABase-*")
        if(EXTRACTED_DIR)
            file(COPY "${EXTRACTED_DIR}/" DESTINATION "${EABASE_SOURCE_DIR}")
            file(REMOVE_RECURSE "${EXTRACTED_DIR}")
        endif()
    endif()

    if(NOT EXISTS "${EASTL_SOURCE_DIR}")
        message(STATUS "Extracting EASTL to ${EASTL_SOURCE_DIR}")
        file(MAKE_DIRECTORY "${EASTL_SOURCE_DIR}")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xf "${CMAKE_SOURCE_DIR}/third_party/EASTL/EASTL-master.zip"
            WORKING_DIRECTORY "${EASTL_PREFIX}"
        )
        file(GLOB EXTRACTED_DIR "${EASTL_PREFIX}/EASTL-*")
        if(EXTRACTED_DIR)
            file(COPY "${EXTRACTED_DIR}/" DESTINATION "${EASTL_SOURCE_DIR}")
            file(REMOVE_RECURSE "${EXTRACTED_DIR}")
        endif()
    endif()

    # 创建EABase库
    add_library(EABase INTERFACE)
    target_include_directories(EABase INTERFACE 
        $<BUILD_INTERFACE:${EABASE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )

    # 创建EASTL库 - 使用单个源文件
    set(EASTL_IMPL_FILE "${CMAKE_BINARY_DIR}/eastl_impl.cpp")
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/modules/eastl_impl.cpp.in"
        "${EASTL_IMPL_FILE}"
        @ONLY
    )

    add_library(EASTL STATIC "${EASTL_IMPL_FILE}")

    target_include_directories(EASTL
        PUBLIC
            $<BUILD_INTERFACE:${EASTL_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${EABASE_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${EASTL_SOURCE_DIR}/source
    )

    target_link_libraries(EASTL
        PUBLIC
            EABase
    )

    # 设置编译定义
    target_compile_definitions(EASTL
        PUBLIC
            EA_PLATFORM_MICROSOFT
            _CHAR16T
            _HAS_CXX17=1
            $<$<CONFIG:DEBUG>:EA_DEBUG>
    )

    if(MSVC)
        target_compile_options(EASTL
            PRIVATE
                /W4
                /WX-
                /wd4100 # unreferenced formal parameter
                /wd4127 # conditional expression is constant
                /wd4324 # structure was padded due to alignment specifier
                /wd4505 # unreferenced local function removed
                /wd4702 # unreachable code
        )
    endif()

    # 设置导出
    install(TARGETS EASTL EABase
        EXPORT TinaTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include
    )

    # 安装头文件
    install(DIRECTORY 
        "${EASTL_SOURCE_DIR}/include/"
        "${EABASE_SOURCE_DIR}/include/"
        DESTINATION include
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
    )
endfunction() 