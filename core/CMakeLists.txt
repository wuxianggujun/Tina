set(SDL3_SOURCES_DIR "${CMAKE_SOURCE_DIR}/libs/SDL3")
set(SDL3_INCLUDE_DIR "${SDL3_SOURCES_DIR}/include")
set(SDL3_LIB_DIR "${SDL3_SOURCES_DIR}/lib")

file(GLOB_RECURSE CORE_SRC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

find_library(SDL3MAIN_LIB SDL3 ${SDL3_LIB_DIR})

file(GLOB SDL3_LIBS "${SDL3_LIB_DIR}/*.lib")

# 复制SDL3可执行文件（如果需要）
file(COPY "${SDL3_SOURCES_DIR}/bin/" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

# 显示SDL3_INCLUDE_DIR
message(STATUS "SDL3_INCLUDE_DIR: ${SDL3_INCLUDE_DIR}")
message(STATUS "CORE_SRC: ${CORE_SRC}")

add_executable(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PUBLIC ${CORE_SRC})
target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(${PROJECT_NAME} PRIVATE ${SDL3_INCLUDE_DIR})
target_link_directories(${PROJECT_NAME} PRIVATE ${SDL3_LIB_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL3MAIN_LIB} ${SDL3_LIBS})

# Add compiler errors/warnings flags
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wunreachable-code -Wpedantic)
endif()
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wweak-vtables -Wexit-time-destructors -Wglobal-constructors -Wmissing-noreturn)
endif()
if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /w44265 /w44061 /w44062)
endif()
