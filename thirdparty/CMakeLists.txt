# freetype (font rendering) - must be before RmlUi
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
add_subdirectory(freetype)

# Create alias for RmlUi
if(TARGET freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()

# bgfx and dependencies (bx, bimg)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(bgfx.cmake)

# spdlog (logging)
add_subdirectory(spdlog)

# glm (math library)
add_subdirectory(glm)

# RmlUi (UI library)
set(NO_FONT_INTERFACE_DEFAULT OFF CACHE BOOL "" FORCE)
set(NO_THIRDPARTY_CONTAINERS OFF CACHE BOOL "" FORCE)
add_subdirectory(rmlui)

# Tracy (profiler)
option(TRACY_ENABLE "" ON)
option(TRACY_ON_DEMAND "" ON)
add_subdirectory(tracy)

# Box2D (physics)
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_DOCS OFF CACHE BOOL "" FORCE)
add_subdirectory(box2d)

# lz4 (compression)
set(LZ4_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LZ4_BUILD_LEGACY_LZ4C OFF CACHE BOOL "" FORCE)
add_subdirectory(lz4/build/cmake)

# xxHash (hashing)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(XXHASH_BUILD_XXHSUM OFF CACHE BOOL "" FORCE)
add_subdirectory(xxHash/build/cmake)

# utfcpp (UTF-8 library)
set(UTF8_TESTS OFF CACHE BOOL "" FORCE)
set(UTF8_INSTALL OFF CACHE BOOL "" FORCE)
set(UTF8_SAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(utfcpp)

# EASTL (Electronic Arts STL) and its dependency EABase
# Don't add EABase manually, let EASTL handle it through FetchContent
# but point it to our local directory

# Prevent EASTL from downloading EABase by pointing to local directory
set(FETCHCONTENT_SOURCE_DIR_EABASE "${CMAKE_CURRENT_SOURCE_DIR}/EABase" CACHE PATH "" FORCE)
set(EASTL_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(EASTL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(EASTL)