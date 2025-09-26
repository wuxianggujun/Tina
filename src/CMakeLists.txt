set(SRC_FILES core/core.hpp core/os.hpp main.cpp)

add_executable(${PROJECT_NAME} ${SRC_FILES})

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
)
