set(SRC_FILES
    main.cpp
    engine/Resource.hpp
    engine/FileSystem.cpp
    engine/Application.hpp
    engine/Application.cpp
    engine/Font.hpp
    engine/Font.cpp
    engine/AudioResource.hpp
    engine/AudioResource.cpp
    engine/AudioEngine.hpp
    engine/AudioEngine.cpp
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
    engine/SceneStateManager.hpp
    engine/SceneStateManager.cpp
    engine/EventCore.hpp
    engine/EventQueue.hpp
    engine/EventDispatcher.hpp
    engine/EventSystem.hpp
    engine/EventSystem.cpp
    engine/Events.hpp
    engine/SubscriptionToken.hpp
    engine/SubscriptionToken.cpp
    engine/Window.hpp
    engine/Window.cpp
    engine/InputSystem.hpp
    engine/InputSystem.cpp
    game/GameScene.hpp
    game/GameScene.cpp
    game/MenuScene.hpp
    game/MenuScene.cpp
    game/PauseScene.hpp
    game/PauseScene.cpp
    game/SettingsScene.hpp
    game/SettingsScene.cpp
    game/WorldSelectScene.hpp
    game/WorldSelectScene.cpp
    game/DayNight.hpp
    particles/ParticleSystem.hpp
    particles/ParticleSystem.cpp
    physics/Physics2D.hpp
    physics/Physics2D.cpp
    engine/Camera2D.hpp
    engine/Camera2D.cpp
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
    renderer/Primitive2D.hpp
    renderer/Primitive2D.cpp
    renderer/ShaderCatalog.hpp
    renderer/ShaderCatalog.cpp
    renderer/SpriteRenderer.hpp
    renderer/SpriteRenderer.cpp
    renderer/RenderCommand.hpp
    renderer/RenderQueue.hpp
    renderer/RenderQueue.cpp
    renderer/RenderCommandBuilder.hpp
    ui/TextRenderer.hpp
    ui/TextRenderer.cpp
    ui/UIBatch.hpp
    ui/BatchStrategy.hpp
    ui/BatchStrategy.cpp
    ui/UIPerformanceMonitor.hpp
    ui/UIPerformanceMonitor.cpp
    ui/UICore.hpp
    ui/UICore.cpp
    ui/UINode.hpp
    ui/UINode.cpp
    ui/UIComponents.hpp
    ui/UIComponents.cpp
    ui/UIToolbar.hpp
    ui/UIToolbar.cpp
    # 旧的 ui/UILayout.hpp 已移除，使用新的容器头文件（仅作为头文件，无需列入构建清单）
    ui/UILayoutManager.hpp
    ui/UILayoutManager.cpp
    ui/UIProgressBar.hpp
    ui/UIProgressBar.cpp
    ui/UICharacterPanel.hpp
    ui/UICharacterPanel.cpp
    ui/UIListView.hpp
    ui/UIListView.cpp
    ui/UITextEdit.hpp
    ui/UITextEdit.cpp
    ui/UIDialog.hpp
    ui/UIDialog.cpp

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

add_subdirectory(platform/audio)

add_executable(${PROJECT_NAME} ${SRC_FILES})
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_EXTENSIONS OFF)

# Include directories for project source files
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 着色器编译
include(ShaderUtils)
add_shader_compile_dir("${RESOURCE_DIR}/shaders" flat color text sprite particle ui world)
add_dependencies(${PROJECT_NAME} shaders)

# 传入 shader 根目录给运行时
set(TINA_SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/resources/shaders")
file(TO_CMAKE_PATH "${TINA_SHADER_BIN_DIR}" TINA_SHADER_BIN_DIR_NORM)
target_compile_definitions(${PROJECT_NAME} PRIVATE TINA_SHADER_ROOT_DIR="${TINA_SHADER_BIN_DIR_NORM}")

# 统一 defines
if(UNIX AND NOT APPLE AND TINA_BUILD_WAYLAND)
    target_compile_definitions(${PROJECT_NAME} PRIVATE TINA_GLFW_ENABLE_WAYLAND=1)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    Tina::ProjectOptions
    Tina::CoreLegacy
    Tina::Procedural
    bgfx
    bx
    bimg
    bimg_decode
    glfw
    Tina::Miniaudio
    EnTT::EnTT
    spdlog::spdlog
    glm::glm
    Freetype::Freetype
    box2d::box2d
    utf8cpp::utf8cpp
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
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}> "${BIN_DIR}"
        COMMAND_EXPAND_LISTS
        COMMENT "Copying runtime DLLs to bin directory"
    )
endif()
