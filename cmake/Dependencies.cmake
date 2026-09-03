include_guard(GLOBAL)

# All package names and imported targets in this file come from the pinned vcpkg registry in
# vcpkg.json. A vNext-only Null configure deliberately discovers none of the Legacy packages.
#
# There are no unconditional root dependencies. xxHash (Core hashing) and MikkTSpace (the glTF
# Cook's tangent generator) used to be two, and both were invisible to consumer code yet reached
# the installed export as $<LINK_ONLY:...> -- so a find_package(Tina) failed naming *those*
# packages, which reads as a broken Tina install. Both are vendored under thirdparty/ now; see
# src/core/CMakeLists.txt and src/asset/CMakeLists.txt.

if (TINA_BUILD_PHYSICS2D)
    find_package(box2d CONFIG REQUIRED)
endif ()

if (TINA_BUILD_NETWORK_TLS)
    # TLS adapter: mbedTLS is driven through caller-supplied BIO callbacks, so it
    # never owns the socket or a thread. mbedTLS types stay inside src/network/tls.
    find_package(MbedTLS CONFIG REQUIRED)
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
