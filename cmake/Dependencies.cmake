include_guard(GLOBAL)

# All package names and imported targets in this file come from the pinned vcpkg registry in
# vcpkg.json. A vNext-only Null configure deliberately discovers none of the Legacy packages.
if(TINA_BUILD_LEGACY)
    find_package(box2d CONFIG REQUIRED)
    find_package(EnTT CONFIG REQUIRED)
    find_package(Freetype REQUIRED)
    find_package(glfw3 3.4 CONFIG REQUIRED)
    find_package(glm CONFIG REQUIRED)
    find_package(spdlog CONFIG REQUIRED)
    find_package(utf8cpp CONFIG REQUIRED)
    find_package(xxHash CONFIG REQUIRED)

    # The miniaudio vcpkg port intentionally installs only miniaudio.h. Tina owns the single
    # MINIAUDIO_IMPLEMENTATION translation unit in src/platform/audio.
    find_path(TINA_MINIAUDIO_INCLUDE_DIR NAMES miniaudio.h REQUIRED)
endif()

if(TINA_BUILD_PLATFORM_GLFW AND NOT TINA_BUILD_LEGACY)
    find_package(glfw3 3.4 CONFIG REQUIRED)
endif()

if(TINA_BUILD_TESTING)
    find_package(GTest 1.17.0 EXACT CONFIG REQUIRED)
endif()
