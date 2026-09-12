# MyGame -- a Tina game project.
#
# Layout, and the one rule that makes it portable:
#
#   core/                 content, linked through the one Tina::GameSDK archive.
#   platforms/desktop/    Windows + Linux + macOS composition root.
#   platforms/web/        browser composition root (Emscripten).
#   assets/               source assets and the cook recipe.
#
# core/ is compiled once and linked by every frontend, so gameplay is written a single time.
# Keep platform APIs in the frontend; a link graph cannot prove C++ portability.

cmake_minimum_required(VERSION 3.25)
project(MyGame LANGUAGES CXX)

# Two ways to reach the engine. An installed SDK is the normal one; the guard skips it when
# this project is add_subdirectory'd into a Tina checkout, where the targets already exist.
#
# Components assert compiled capabilities; they never select another library.
if(NOT TARGET Tina::GameSDK)
    if(EMSCRIPTEN)
        find_package(Tina 0.3.0 EXACT CONFIG REQUIRED COMPONENTS GameSDK)
    else()
        find_package(Tina 0.3.0 EXACT CONFIG REQUIRED COMPONENTS Desktop)
    endif()
endif()

add_subdirectory(core)

# Frontends are opt-in per platform: a browser build has no desktop bootstrap, and a desktop
# build has no Emscripten toolchain, so neither can be unconditional.
#
# A web frontend goes here as platforms/web/ using tina_add_web_frontend(); this template
# ships no web entry point yet, because the browser composition root is still hand-rolled in
# samples/web/platforms/web/WebMain.cpp with no Tina::Web::CreateEngine to call. Copying those
# into a template would fork them. See README.md.
if(EMSCRIPTEN)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/platforms/web/CMakeLists.txt")
        add_subdirectory(platforms/web)
    else()
        message(FATAL_ERROR
            "No platforms/web/ in this project. Create it with tina_add_web_frontend() and "
            "base its entry point on samples/web/platforms/web/WebMain.cpp -- see README.md.")
    endif()
else()
    add_subdirectory(platforms/desktop)
endif()

# Validate game-owned dependencies without expanding the private SDK implementation.
tina_validate_content_dependencies(mygame_content)
