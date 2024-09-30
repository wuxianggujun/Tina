if (MSVC)
    add_compile_options(/W3)
else ()
    add_compile_options(-w)
endif ()

# glfw
set(GLFW_BUILD_EXAMPLES FALSE CACHE INTERNAL "")
set(GLFW_BUILD_TESTS FALSE CACHE INTERNAL "")
set(GLFW_BUILD_DOCS FALSE CACHE INTERNAL "")
set(GLFW_INSTALL FALSE CACHE INTERNAL "")

set(GLFW_BUILD_WAYLAND ON )
#set(GLFW_USE_WAYLAND ON)
#set(GLFW_BUILD_X11 OFF)


# glm
set(GLM_TEST_ENABLE OFF CACHE INTERNAL "")
set(GLM_ENABLE_CXX_20 TRUE CACHE INTERNAL "")
set(GLM_DISABLE_AUTO_DETECTION TRUE CACHE INTERNAL "")
set(GLM_QUIET TRUE CACHE INTERNAL "")

# bgfx
set(BGFX_BUILD_TOOLS TRUE CACHE INTERNAL "")
set(BGFX_BUILD_EXAMPLES TRUE CACHE INTERNAL "")
set(BGFX_CUSTOM_TARGETS FALSE CACHE INTERNAL "")
set(BGFX_BUILD_EXAMPLE_COMMON FALSE CACHE INTERNAL "")



# googletest
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# entt
set(BUILD_TESTING OFF CACHE INTERNAL "")
option(ENTT_INCLUDE_NATVIS "Add EnTT natvis files to the EnTT target." ON)

# tracy
set(TRACY_ENABLE ON CACHE BOOL "Enable profiling")
#set(TRACY_NO_SYSTEM_TRACING ON CACHE BOOL "Disable System Tracing")
set(TRACY_ONLY_IPV4 ON CACHE BOOL "IPv4 only")
option(TRACY_ENABLE "Enable profiling" ON)
OPTION(TRACY_ON_DEMAND "" ON)
#option(TRACY_NO_SYSTEM_TRACING "Disable System Tracing" ON)
option(TRACY_ONLY_IPV4 "IPv4 only" ON)

# icu
set(BUILD_ICU ON CACHE BOOL "Enable compilation of ICU")
set(ICU_BUILD_VERSION "72.1" CACHE STRING "" FORCE)
#set(ICU_STATIC ON CACHE BOOL "Use static instead of shared ICU (does not work for system)")

add_subdirectory_checked(bgfx.cmake)
#find_package(GLFW3 REQUIRED)
find_package(X11 REQUIRED)

add_subdirectory_checked(glfw)
add_subdirectory_checked(glm)
add_subdirectory_checked(fmt)
add_subdirectory_checked(yaml-cpp)
add_subdirectory_checked(spdlog)
add_subdirectory_checked(stb-cmake)
add_subdirectory_checked(entt)
add_subdirectory_checked(googletest)
add_subdirectory_checked(tracy)
add_subdirectory_checked(ctrack)
add_subdirectory_checked(icu/icu4c)

set(TINA_LIBRARIES
        bx
        bimg
        bimg_decode
        bgfx
        glfw
        glm
        icu
        fmt::fmt
        spdlog
        stb::image
        EnTT::EnTT
        yaml-cpp::yaml-cpp
        Tracy::TracyClient
        ctrack
        CACHE INTERNAL "")
