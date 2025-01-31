include(ExternalProject)

function(setup_eastl)
    set(EASTL_PREFIX "${CMAKE_BINARY_DIR}/EASTL")

    # 首先配置EABase
    ExternalProject_Add(EABase_external
        URL "${CMAKE_SOURCE_DIR}/third_party/EASTL/EABase-master.zip"
        URL_HASH ""
        TLS_VERIFY OFF
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        PREFIX ${EASTL_PREFIX}/EABase
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        INSTALL_COMMAND ""
    )

    # 获取EABase的属性
    ExternalProject_Get_Property(EABase_external SOURCE_DIR)
    set(EABASE_SOURCE_DIR ${SOURCE_DIR})

    # 创建必要的目录
    file(MAKE_DIRECTORY "${EABASE_SOURCE_DIR}/include")

    # 创建EABase库
    add_library(EABase INTERFACE)
    add_dependencies(EABase EABase_external)
    target_include_directories(EABase INTERFACE 
        $<BUILD_INTERFACE:${EABASE_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${EABASE_SOURCE_DIR}/include/Common>
    )

    # 然后配置EASTL
    ExternalProject_Add(EASTL_external
        DEPENDS EABase_external
        URL "${CMAKE_SOURCE_DIR}/third_party/EASTL/EASTL-master.zip"
        URL_HASH ""
        TLS_VERIFY OFF
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        PREFIX ${EASTL_PREFIX}
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DEASTL_BUILD_TESTS=OFF
            -DEASTL_BUILD_BENCHMARK=OFF
            -DEASTL_STD_ITERATOR_CATEGORY_ENABLED=ON
            -DCMAKE_INSTALL_PREFIX=${EASTL_PREFIX}
            -DEABase_DIR=${EABASE_SOURCE_DIR}
        BUILD_BYPRODUCTS 
            ${EASTL_PREFIX}/src/EASTL_external-build/EASTL.lib
        INSTALL_COMMAND ""
    )

    # 获取EASTL的属性
    ExternalProject_Get_Property(EASTL_external SOURCE_DIR BINARY_DIR)

    # 创建必要的目录
    file(MAKE_DIRECTORY "${SOURCE_DIR}/include")

    # 创建EASTL库
    add_library(EASTL STATIC IMPORTED GLOBAL)
    add_dependencies(EASTL EASTL_external EABase)

    # 设置EASTL的属性
    if(WIN32)
        set_target_properties(EASTL PROPERTIES
            IMPORTED_LOCATION "${BINARY_DIR}/EASTL.lib"
        )
    else()
        set_target_properties(EASTL PROPERTIES
            IMPORTED_LOCATION "${BINARY_DIR}/libEASTL.a"
        )
    endif()

    # 设置包含目录
    target_include_directories(EASTL INTERFACE
        $<BUILD_INTERFACE:${SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${EABASE_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${EABASE_SOURCE_DIR}/include/Common>
    )
endfunction() 