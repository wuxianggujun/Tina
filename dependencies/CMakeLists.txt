if(MSVC)
  add_compile_options(/W3)
else()
  add_compile_options(-w)
endif()

# glfw
set(GLFW_BUILD_EXAMPLES FALSE CACHE INTERNAL "")
set(GLFW_BUILD_TESTS FALSE CACHE INTERNAL "")
set(GLFW_BUILD_DOCS FALSE CACHE INTERNAL "")
set(GLFW_INSTALL FALSE CACHE INTERNAL "")

#glm
set(GLM_TEST_ENABLE  OFF CACHE INTERNAL "")
set(GLM_ENABLE_CXX_20 TRUE CACHE INTERNAL "")
set(GLM_DISABLE_AUTO_DETECTION TRUE CACHE INTERNAL "")
set(GLM_QUIET TRUE CACHE INTERNAL "")

#bgfx
set(BGFX_BUILD_TOOLS TRUE CACHE INTERNAL "")
set(BGFX_BUILD_EXAMPLES TRUE CACHE INTERNAL "")
set(BGFX_CUSTOM_TARGETS FALSE CACHE INTERNAL "")
set(BGFX_BUILD_EXAMPLE_COMMON FALSE CACHE INTERNAL "")

#googletest
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

#entt
set(BUILD_TESTING OFF CACHE INTERNAL "")
option(ENTT_INCLUDE_NATVIS "Add EnTT natvis files to the EnTT target." ON)

#tracy
set(TRACY_ENABLE ON CACHE BOOL "Enable profiling")
#set(TRACY_NO_SYSTEM_TRACING ON CACHE BOOL "Disable System Tracing")
set(TRACY_ONLY_IPV4 ON CACHE BOOL "IPv4 only")
option(TRACY_ENABLE "Enable profiling" ON)
OPTION(TRACY_ON_DEMAND "" ON)
#option(TRACY_NO_SYSTEM_TRACING "Disable System Tracing" ON)
option(TRACY_ONLY_IPV4 "IPv4 only" ON)

add_subdirectory(bgfx.cmake)
#add_subdirectory(GSL)

#find_package(GLFW3 REQUIRED)
find_package(X11 REQUIRED)

add_subdirectory(glfw)
add_subdirectory(SDL)
#add_subdirectory(glad)
add_subdirectory(glm)
add_subdirectory(yaml-cpp)
add_subdirectory(spdlog)
add_subdirectory(stb-cmake)
add_subdirectory(entt)
add_subdirectory(googletest)
add_subdirectory(tracy)
add_subdirectory(ctrack)

set(TINA_LIBRARIES
    bx
    bimg
    bgfx
    glfw
    glm
    spdlog
    stb::image
    EnTT::EnTT 
    SDL3::SDL3
    yaml-cpp::yaml-cpp
    Tracy::TracyClient 
    ctrack
    CACHE INTERNAL "")
