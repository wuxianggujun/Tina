cmake_minimum_required(VERSION 3.12)
project(glad C)

set(GLAD_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(GLAD_FILES "")

list(APPEND GLAD_FILES
        "${GLAD_DIR}/include/glad/gl.h"
        "${GLAD_DIR}/include/KHR/khrplatform.h"
        "${GLAD_DIR}/src/gl.c"
)

add_library(${PROJECT_NAME} STATIC ${GLAD_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE include SYSTEM INTERFACE include)