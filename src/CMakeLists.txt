set(SRC_FILES
    main.cpp
    os/OS.cpp
    engine/Resource.hpp
    engine/FileSystem.cpp
    core/Path.cpp
    core/EASTLAlloc.cpp
    core/Noise.hpp
    core/Noise.cpp
    particles/ParticleSystem.hpp
    particles/ParticleSystem.cpp
    physics/Physics2D.hpp
    physics/Physics2D.cpp
    game/Camera2D.hpp
    game/Camera2D.cpp
    game/TileMap.hpp
    game/TileMap.cpp
    game/Player.hpp
    game/Player.cpp
    renderer/Renderer.hpp
    renderer/Pipeline.hpp
    renderer/SortKey.hpp
    renderer/ShaderManager.hpp
    renderer/ShaderManager.cpp
    ui/TextRenderer.hpp
    ui/TextRenderer.cpp
    ui/UICore.hpp
    ui/UICore.cpp
    ui/UINode.hpp
    ui/UINode.cpp
    ui/UIComponents.hpp
    ui/UIComponents.cpp
    ui/UIEventSystem.hpp
    ui/UIEventSystem.cpp
    ui/UIToolbar.hpp
    ui/UIToolbar.cpp
    ui/UILayout.hpp
)

add_executable(${PROJECT_NAME} ${SRC_FILES})

# Include directories for project source files
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 着色器编译
include(ShaderUtils)
add_shader_compile_dir("${RESOURCE_DIR}/shaders" flat color text sprite particle)
add_dependencies(${PROJECT_NAME} shaders)

# 传入 shader 根目录给运行时
set(TINA_SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/resources/shaders")
file(TO_CMAKE_PATH "${TINA_SHADER_BIN_DIR}" TINA_SHADER_BIN_DIR_NORM)
target_compile_definitions(${PROJECT_NAME} PRIVATE TINA_SHADER_ROOT_DIR="${TINA_SHADER_BIN_DIR_NORM}")

# 统一 defines
target_compile_definitions(${PROJECT_NAME} PRIVATE BUILDING_CORE)

# SDL3 依赖
if (TARGET SDL3::SDL3)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3)
elseif (TARGET SDL3::SDL3-shared)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3-shared)
else()
    find_package(SDL3 CONFIG QUIET)
    if (SDL3_FOUND)
        target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3)
    else ()
        message(FATAL_ERROR "未找到 SDL3，请确保 thirdparty/SDL 通过 add_subdirectory 引入，或系统已安装 SDL3 并提供 SDL3Config.cmake")
    endif ()
endif ()

target_link_libraries(${PROJECT_NAME} PRIVATE
    bgfx
    bx
    bimg
    bimg_decode
    spdlog::spdlog
    glm::glm
    freetype
    TracyClient
    box2d
    lz4_static
    xxhash
    utf8cpp
    EASTL
)

# 确保 bin 目录存在
file(MAKE_DIRECTORY "${BIN_DIR}")

# 拷贝可执行文件到 bin
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BIN_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${PROJECT_NAME}> "${BIN_DIR}/"
    COMMENT "Copying executable to bin directory"
)

# 拷贝资源到 bin/resources
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BIN_DIR}/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/resources" "${BIN_DIR}/resources"
    COMMENT "Copying resources to bin directory"
)

# Windows 上拷贝运行时 DLL
if(WIN32)
    if(TARGET SDL3::SDL3-shared)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:SDL3::SDL3-shared> "${BIN_DIR}/"
            COMMENT "Copying SDL3 DLL to bin directory"
        )
    endif()

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}> "${BIN_DIR}"
        COMMAND_EXPAND_LISTS
        COMMENT "Copying runtime DLLs to bin directory"
    )
endif()
