set(SRC_FILES
    main.cpp
    os/OS.cpp
    engine/Resource.hpp
    engine/FileSystem.cpp
    engine/Application.hpp
    engine/Application.cpp
    engine/Font.hpp
    engine/Font.cpp
    engine/AudioResource.hpp
    engine/AudioResource.cpp
    engine/AudioManager.hpp
    engine/Texture.hpp
    engine/Texture.cpp
    engine/Scene.hpp
    engine/Scene.cpp
    engine/SceneRenderer.hpp
    engine/SceneRenderer.cpp
    engine/Scene.cpp
    engine/SceneManager.hpp
    engine/SceneManager.cpp
    engine/EventBus.hpp
    engine/EventBus.cpp
    game/GameScene.hpp
    game/GameScene.cpp
    game/MenuScene.hpp
    game/MenuScene.cpp
    game/PauseScene.hpp
    game/PauseScene.cpp
    game/SettingsScene.hpp
    game/SettingsScene.cpp
    game/DayNight.hpp
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
    game/CoordinateMapper.hpp
    game/TerrainEditor.hpp
    game/GameConfig.hpp
    renderer/Renderer.hpp
    renderer/Pipeline.hpp
    renderer/SortKey.hpp
    renderer/ShaderManager.hpp
    renderer/ShaderManager.cpp
    renderer/TileRenderer.hpp
    renderer/TileRenderer.cpp
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
    ui/UIProgressBar.hpp
    ui/UIProgressBar.cpp
    ui/UICharacterPanel.hpp
    ui/UICharacterPanel.cpp

    # ECS
    ecs/World.hpp
    ecs/World.cpp
    ecs/Components.hpp
    ecs/systems/PlayerInputSystem.hpp
    ecs/systems/PlayerInputSystem.cpp
    ecs/systems/CharacterMovementSystem.hpp
    ecs/systems/CharacterMovementSystem.cpp
    ecs/systems/PhysicsSystem.hpp
    ecs/systems/PhysicsSystem.cpp
    ecs/systems/AISystem.hpp
    ecs/systems/AISystem.cpp
    ecs/systems/CharacterRenderSystem.hpp
    ecs/systems/CharacterRenderSystem.cpp
)

add_executable(${PROJECT_NAME} ${SRC_FILES})

# Include directories for project source files
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/thirdparty/entt/single_include
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

# SDL_mixer 依赖（音频混音库）
if (TARGET SDL3_mixer::SDL3_mixer)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL3_mixer::SDL3_mixer)
    message(STATUS "SDL_mixer 已链接")
elseif (TARGET SDL3_mixer::SDL3_mixer-shared)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL3_mixer::SDL3_mixer-shared)
    message(STATUS "SDL_mixer (shared) 已链接")
else()
    message(WARNING "SDL_mixer 未找到，音频功能将受限")
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

# 先同步源码 resources（排除 resources/shaders）到构建目录的 resources
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DSRC="${CMAKE_SOURCE_DIR}/resources" -DDST="${CMAKE_BINARY_DIR}/resources" -P "${CMAKE_SOURCE_DIR}/cmake/CopyResources.cmake"
    COMMENT "Syncing resources (exclude resources/shaders) into build directory"
)

# 再将构建目录的 resources 整体复制到运行目录（保留构建生成的 resources/shaders 编译产物）
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
