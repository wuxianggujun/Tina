set(SRC_FILES core/core.hpp os/os.hpp os/win/os.cpp os/win/Windowpp.hpp os/win/Windowpp.cpp main.cpp)

add_executable(${PROJECT_NAME} ${SRC_FILES})

# Include directories for project source files
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

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
