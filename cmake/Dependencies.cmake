include_guard(GLOBAL)

# All package names and imported targets in this file come from the pinned vcpkg registry in
# vcpkg.json. A vNext-only Null configure deliberately discovers none of the Legacy packages.
#
# Source audio decoding is a base capability, including in headless cookers.
# Device I/O remains optional. SDK consumers resolve the same private codec link
# closure through TinaConfig, without including any third-party codec headers.
find_path(TINA_MINIAUDIO_INCLUDE_DIR NAMES miniaudio.h REQUIRED)
find_package(Vorbis CONFIG REQUIRED)
find_package(Opus CONFIG REQUIRED)
find_package(OpusFile CONFIG REQUIRED)

if(TINA_TRACE_BACKEND STREQUAL "tracy")
    # The single runtime archive is finalized at the root. Imported targets
    # discovered only in src/trace/tracy are invisible to that parent link scope.
    find_package(Tracy CONFIG REQUIRED)
endif()

if (TINA_BUILD_PHYSICS2D)
    find_package(box2d CONFIG REQUIRED)
endif ()

if (TINA_BUILD_PHYSICS3D)
    # The pinned port has no ConfigVersion file. The adapter checks Jolt's
    # version macros at compile time instead of accepting an unverified API.
    find_package(Jolt CONFIG REQUIRED)
endif ()

if (TINA_BUILD_NETWORK_TLS)
    # TLS adapter: mbedTLS is driven through caller-supplied BIO callbacks, so it
    # never owns the socket or a thread. mbedTLS types stay inside src/network/tls.
    find_package(MbedTLS CONFIG REQUIRED)
endif ()

if(TINA_BUILD_PLATFORM_GLFW)
    find_package(glfw3 3.4 CONFIG REQUIRED)
endif()

if(TINA_BUILD_UI_FREETYPE)
    find_package(Freetype REQUIRED)
    find_package(HarfBuzz MODULE REQUIRED)
    find_package(FriBidi REQUIRED)
    find_package(msdfgen CONFIG REQUIRED)
endif()

if(TINA_BUILD_TESTING)
    find_package(GTest 1.17.0 EXACT CONFIG REQUIRED)
endif()
