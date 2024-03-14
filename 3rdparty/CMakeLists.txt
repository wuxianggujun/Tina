cmake_minimum_required(VERSION 3.20)
project(3rdparty)

# glfw
set(GLFW_BUILD_EXAMPLES FALSE CACHE INTERNAL "")
set(GLFW_BUILD_TESTS FALSE CACHE INTERNAL "")
set(GLFW_BUILD_DOCS FALSE CACHE INTERNAL "")
set(GLFW_INSTALL FALSE CACHE INTERNAL "")

#glm
set(GLM_ENABLE_CXX_20 TRUE CACHE INTERNAL "")
set(GLM_DISABLE_AUTO_DETECTION TRUE CACHE INTERNAL "")
set(GLM_QUIET TRUE CACHE INTERNAL "")

#bgfx
set(BGFX_BUILD_TOOLS TRUE CACHE INTERNAL "")
set(BGFX_BUILD_EXAMPLES TRUE CACHE INTERNAL "")
set(BGFX_CUSTOM_TARGETS FALSE CACHE INTERNAL "")


add_subdirectory(bgfx.cmake)
add_subdirectory(microsoft-gsl)
add_subdirectory(glfw)
add_subdirectory(glm)
add_subdirectory(spdlog)
add_subdirectory(stb-cmake)
