cmake_minimum_required(VERSION 3.20..3.28)

project(framework)

set(FRAMEWORK_DIR "${CMAKE_CURRENT_LIST_DIR}")

file(GLOB FRAMEWORK_SOURCES CONFIGURE_DEPENDS
    "${FRAMEWORK_DIR}/*.cpp"
    "${FRAMEWORK_DIR}/*.hpp")

source_group("framework" FILES ${FRAMEWORK_SOURCES})

add_library(${PROJECT_NAME} STATIC ${FRAMEWORK_SOURCES})
target_link_libraries(${PROJECT_NAME} PUBLIC glfw glm bx bimg bgfx)
target_link_libraries(${PROJECT_NAME} PUBLIC Boost::predef Boost::utility Boost::core Boost::detail Boost::lexical_cast Boost::exception Boost::timer)
target_link_libraries(${PROJECT_NAME} PUBLIC spdlog stb::image Microsoft.GSL::GSL EnTT::EnTT)

target_link_libraries(${PROJECT_NAME} PUBLIC "${SHADERS_TARGET_NAME}")

target_compile_definitions(${PROJECT_NAME} PUBLIC GLFW_INCLUDE_NONE)
