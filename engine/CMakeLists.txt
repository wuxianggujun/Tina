# 设置引擎库的版本
set(TINA_ENGINE_VERSION ${PROJECT_VERSION})

# 添加 cmake 模块路径
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

# 包含资源复制模块
include(CopyResources)
include(ExternalProject)

# 配置EASTL
set(EASTL_PREFIX "${CMAKE_BINARY_DIR}/EASTL")

# 首先配置EABase
ExternalProject_Add(EABase_external
    URL "${CMAKE_SOURCE_DIR}/third_party/EASTL/EABase-master.zip"
    URL_HASH ""
    TLS_VERIFY OFF
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

# 添加第三方库
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake bgfx_build)

# 添加子模块
add_subdirectory(core)
#add_subdirectory(network)
#add_subdirectory(utils)

# 创建引擎总库（作为接口库）
add_library(TinaEngine INTERFACE)
add_library(Tina::Engine ALIAS TinaEngine)

# 链接所有子模块
target_link_libraries(TinaEngine
    INTERFACE
        Tina::Core
#        Tina::Network
#        Tina::Utils
)

# 复制资源文件
copy_resources(
    TARGET_NAME TinaEngine
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    DESTINATION_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources"
    PATTERNS
        "shaders/*"
        "textures/*"
        "icons/*"
        "fonts/*"
)
# 安装资源文件
# install(DIRECTORY resources/
#         DESTINATION share/tina/resources
#         FILES_MATCHING
#         PATTERN "shaders/*"
#         PATTERN "textures/*"
#         PATTERN "icons/*"
#         PATTERN "fonts/*"
# )

# # 导出并安装CMake配置文件 (关键：将 EASTL 和 EABase 添加到导出列表中)
# install(TARGETS TinaCore EASTL
#         EXPORT TinaTargets
#         LIBRARY DESTINATION lib
#         ARCHIVE DESTINATION lib
#         RUNTIME DESTINATION bin
#         INCLUDES DESTINATION include
# )

# install(EXPORT TinaTargets
#         FILE TinaTargets.cmake
#         NAMESPACE Tina::
#         DESTINATION lib/cmake/Tina
# )
