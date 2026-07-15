include_guard(GLOBAL)

# All package names and imported targets in this file come from the pinned vcpkg registry in
# vcpkg.json. bgfx and EASTL intentionally remain source dependencies under thirdparty/.
find_package(box2d CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(Freetype REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(utf8cpp CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)

if(TINA_BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
endif()

# The miniaudio vcpkg port intentionally installs only miniaudio.h. Tina owns the single
# MINIAUDIO_IMPLEMENTATION translation unit in src/platform/audio.
find_path(TINA_MINIAUDIO_INCLUDE_DIR NAMES miniaudio.h REQUIRED)
