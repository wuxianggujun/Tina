# MyGame -- a Tina game project.
#
# Layout, and the one rule that makes it portable:
#
#   core/                 portable content. May link engine modules, never a platform target.
#   platforms/desktop/    Windows + Linux + macOS composition root.
#   platforms/web/        browser composition root (Emscripten).
#   assets/               source assets and the cook recipe.
#
# core/ is compiled once and linked by every frontend, so gameplay is written a single time.
# The split is enforced rather than documented: tina_add_game_content fails the configure if
# content links a platform target, because that mistake is invisible until a port is
# attempted. See README.md for the asset-pipeline caveats on mobile and web.

cmake_minimum_required(VERSION 3.25)
project(MyGame LANGUAGES CXX)

# Two ways to reach the engine. An installed SDK is the normal one; the guard skips it when
# this project is add_subdirectory'd into a Tina checkout, where the targets already exist.
#
# The component to ask for is a platform bundle, not an engine module: GameSDK is the
# portable set (Runtime, Scene, Asset, UI, Audio, ...) and DesktopBootstrap adds the GLFW and
# bgfx backends plus the CreateEngine helper. There is no "Runtime" component -- naming one
# makes find_package report the whole package as not found, which reads like a broken install
# rather than a wrong argument. See the component list in TinaConfig.cmake.
if(NOT TARGET Tina::Runtime)
    if(EMSCRIPTEN)
        find_package(Tina CONFIG REQUIRED COMPONENTS GameSDK)
    else()
        find_package(Tina CONFIG REQUIRED COMPONENTS DesktopBootstrap)
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
elseif(TARGET Tina::DesktopBootstrap)
    add_subdirectory(platforms/desktop)
endif()

# Run once both content and frontends exist: this walks the finished link graph and catches
# a platform dependency reached indirectly, which the per-call check cannot see.
tina_verify_game_content_portable(mygame_content)
