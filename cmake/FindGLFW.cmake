cmake_minimum_required(VERSION 3.18)

set(GLFW_LIBRARIES)
set(GLFW_DYNAMIC_LIBRARY glfw3.dll)
set(GLFW_STATIC_LIBRARY glfw3.lib)
set(GLFW_SOURCES_DIR "${CMAKE_SOURCE_DIR}/libs/glfw-3.3.9.bin.WIN64")
# 指定库头文件所在路径
find_path(GLFW_INCLUDE_DIR glfw3.h HINTS "${GLFW_SOURCES_DIR}/include/GLFW")
# 查找动态库和静态库
file(GLOB GLFW_STATIC_LIBRARIES "${GLFW_SOURCES_DIR}/lib-vc2022/*.lib" "${GLFW_SOURCES_DIR}/lib-vc2022/*.dll")


set(GLFW_FOUND FALSE)
if (GLFW_INCLUDE_DIR AND GLFW_STATIC_LIBRARIES)
    set(GLFW_FOUND TRUE)
    list(APPEND GLFW_LIBRARIES ${GLFW_STATIC_LIBRARIES})

    message(STATUS "Found GLFW: ${GLFW_LIBRARIES}")
    message(STATUS "Found GLFW include: ${GLFW_INCLUDE_DIR}")

else ()
    message(FATAL_ERROR "Could not find GLFW")
endif ()