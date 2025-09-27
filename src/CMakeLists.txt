set(SRC_FILES
    main.cpp
    os/OS.cpp
)

add_executable(${PROJECT_NAME} ${SRC_FILES})

# Include directories for project source files
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 统一导出/导入宏，单体可执行目标下将核心符号视为“构建中”
target_compile_definitions(${PROJECT_NAME} PRIVATE BUILDING_CORE)

# SDL3：优先使用子项目提供的目标，其次再 find_package（所有平台均使用 SDL3）
if (TARGET SDL3::SDL3)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3)
elseif (TARGET SDL3::SDL3-shared)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3-shared)
else()
    find_package(SDL3 CONFIG QUIET)
    if (SDL3_FOUND)
        target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3)
    else ()
        message(FATAL_ERROR "未找到 SDL3。请确保 thirdparty/SDL 已通过 add_subdirectory 引入，或在系统中安装 SDL3 并提供 SDL3Config.cmake。")
    endif ()
endif ()

target_link_libraries(${PROJECT_NAME} PRIVATE
    bgfx
    bx
    bimg
    spdlog::spdlog
    glm::glm
    freetype
    rmlui_core
    TracyClient
    box2d
    lz4_static
    xxhash
    utf8cpp
    EASTL
)
