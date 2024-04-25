if(MSVC)
  add_compile_options(/W0)
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

add_subdirectory(bgfx.cmake)
add_subdirectory(boost)
add_subdirectory(GSL)
add_subdirectory(glfw)
add_subdirectory(glm)
add_subdirectory(spdlog)
add_subdirectory(stb-cmake)
add_subdirectory(entt)
add_subdirectory(googletest)

set(TINA_LIBRARIES
    bx
    bimg
    bgfx
    Boost::predef
    Boost::utility
    Boost::core Boost::detail
    Boost::lexical_cast
    Boost::exception
    Boost::timer
    Boost::chrono
    glfw
    glm
    spdlog
    stb::image
    Microsoft.GSL::GSL
    EnTT::EnTT
    CACHE INTERNAL "")
