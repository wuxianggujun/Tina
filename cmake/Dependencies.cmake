include_guard(GLOBAL)

# All package names and imported targets in this file come from the pinned vcpkg registry in
# vcpkg.json. A vNext-only Null configure deliberately discovers none of the Legacy packages.
# xxHash is a root dependency: Core PRIVATE ContentHash digest needs it even when Legacy is OFF.
find_package(xxHash CONFIG REQUIRED)

if (TINA_BUILD_PHYSICS2D)
    find_package(box2d CONFIG REQUIRED)
endif ()

if (TINA_BUILD_AUDIO_MINIAUDIO)
    # vNext audio adapter: header-only miniaudio via vcpkg feature audio-miniaudio.
    # Single MINIAUDIO_IMPLEMENTATION TU lives in src/audio/miniaudio.
    find_path(TINA_MINIAUDIO_INCLUDE_DIR NAMES miniaudio.h REQUIRED)
endif()

if (TINA_AUDIO_ENABLE_LIBVORBIS)
    find_package(Vorbis CONFIG REQUIRED)
endif ()
if (TINA_AUDIO_ENABLE_LIBOPUS)
    find_package(Opus CONFIG REQUIRED)
    find_package(OpusFile CONFIG REQUIRED)
endif ()


if(TINA_BUILD_PLATFORM_GLFW)
    find_package(glfw3 3.4 CONFIG REQUIRED)
endif()

if(TINA_BUILD_UI_FREETYPE)
    find_package(Freetype REQUIRED)
endif()

if(TINA_BUILD_TESTING)
    find_package(GTest 1.17.0 EXACT CONFIG REQUIRED)
endif()
