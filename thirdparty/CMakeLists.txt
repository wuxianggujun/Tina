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