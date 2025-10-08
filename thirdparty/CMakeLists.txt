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
# 开启共享库，生成 rmlui_core.dll / rmlui_debugger.dll（Windows 下）
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(NO_FONT_INTERFACE_DEFAULT OFF CACHE BOOL "" FORCE)
set(NO_THIRDPARTY_CONTAINERS OFF CACHE BOOL "" FORCE)
add_subdirectory(rmlui)

# Tracy (profiler)
option(TRACY_ENABLE "" ON)
option(TRACY_ON_DEMAND "" ON)
add_subdirectory(tracy)

# Box2D (physics)
# 开启共享库，生成 box2d.dll（Debug 为 box2dd.dll）
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
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

# SDL3 (window/input abstraction)
# 关闭测试与安装，作为子项目集成
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/SDL/CMakeLists.txt")
    add_subdirectory(SDL)
else()
    message(WARNING "thirdparty/SDL 未找到，若需 SDL3 集成请确保已克隆到 thirdparty/SDL")
endif()

# SDL_mixer (audio mixing library)
# 依赖 SDL3，提供多格式音频支持（MP3/OGG/FLAC/WAV）
# 使用内置解码器（dr_libs, stb_vorbis），无需外部依赖
set(SDL3MIXER_DEPS_SHARED OFF CACHE BOOL "" FORCE)
set(SDL3MIXER_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL3MIXER_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL3MIXER_TESTS OFF CACHE BOOL "" FORCE)
set(SDL3MIXER_VENDORED OFF CACHE BOOL "" FORCE)  # 不使用外部依赖

# 使用内置解码器（单头文件库）
set(SDL3MIXER_MP3_DRMP3 ON CACHE BOOL "" FORCE)           # MP3 解码器（dr_mp3.h）
set(SDL3MIXER_FLAC_DRFLAC ON CACHE BOOL "" FORCE)         # FLAC 解码器（dr_flac.h）
set(SDL3MIXER_VORBIS_STB ON CACHE BOOL "" FORCE)          # OGG 解码器（stb_vorbis.h）
set(SDL3MIXER_WAV ON CACHE BOOL "" FORCE)                 # WAV 解码器（内置）

# 禁用需要外部依赖的解码器
set(SDL3MIXER_MP3_MPG123 OFF CACHE BOOL "" FORCE)         # 外部 mpg123（禁用）
set(SDL3MIXER_VORBIS_VORBISFILE OFF CACHE BOOL "" FORCE)  # 外部 Vorbis（禁用）
set(SDL3MIXER_VORBIS_TREMOR OFF CACHE BOOL "" FORCE)      # 外部 Tremor（禁用）
set(SDL3MIXER_FLAC_LIBFLAC OFF CACHE BOOL "" FORCE)       # 外部 FLAC（禁用）
set(SDL3MIXER_OPUS OFF CACHE BOOL "" FORCE)               # 外部 Opus（禁用）
set(SDL3MIXER_MOD_XMP OFF CACHE BOOL "" FORCE)            # 外部 libxmp（禁用）
set(SDL3MIXER_MIDI_TIMIDITY OFF CACHE BOOL "" FORCE)      # 外部 Timidity（禁用）
set(SDL3MIXER_MIDI_FLUIDSYNTH OFF CACHE BOOL "" FORCE)    # 外部 FluidSynth（禁用）

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/SDL_mixer/CMakeLists.txt")
    add_subdirectory(SDL_mixer)
else()
    message(WARNING "thirdparty/SDL_mixer 未找到，音频功能将不可用")
endif()
