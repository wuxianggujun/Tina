include_guard(GLOBAL)

# Game project scaffolding: one content library, one thin frontend per platform.
#
# The rule this file exists to enforce: game content must not link a platform target. A
# translation unit that references Tina::Desktop::CreateEngine has to link
# Tina::DesktopBootstrap, which pulls in GLFW, which does not exist on Android or in a
# browser -- so that whole file becomes desktop-only. Twelve samples in this tree are
# already unreachable from Android for exactly that reason, and nothing warned them.
#
# tina_add_game_content() refuses the forbidden link at configure time instead, so the
# mistake costs one error message rather than a port.

# Platform targets that must never appear in a content library. Kept as one list so a new
# backend is rejected everywhere by adding it here once.
set(TINA_GAME_CONTENT_FORBIDDEN_LINKS
    Tina::DesktopBootstrap
    Tina::PlatformGlfw
    Tina::PlatformHtml5
    Tina::PlatformAndroid
    Tina::PlatformAndroidJni
)

# Tina::ProjectOptions carries this tree's warning flags. It is an in-tree target that the
# SDK never exports, so it has to be optional: referencing it unconditionally makes every
# function here fail at generate time inside a find_package(Tina) consumer, which is the one
# place this scaffolding most needs to work.
function(tina_game_project_apply_options target)
    if(TARGET Tina::ProjectOptions)
        target_link_libraries(${target} PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>")
    endif()
endfunction()

# The portable half of a game: states, scenes, UI, gameplay. Links Tina::Runtime and
# nothing platform-shaped, which is what lets every frontend link the same library.
#
#   tina_add_game_content(mygame_content SOURCES Game.cpp LINK Tina::Physics2D)
function(tina_add_game_content target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LINK;INCLUDE;DEFINITIONS")
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "tina_add_game_content(${target}) requires SOURCES.")
    endif()

    foreach(forbidden IN LISTS TINA_GAME_CONTENT_FORBIDDEN_LINKS)
        if(forbidden IN_LIST ARG_LINK)
            message(FATAL_ERROR
                "tina_add_game_content(${target}) may not link ${forbidden}.\n"
                "Content has to stay portable: a platform target makes these sources "
                "buildable for one platform only, and the failure does not surface until "
                "someone tries to port them.\n"
                "Move the composition root (CreateEngine and friends) into a frontend "
                "translation unit and pass it to tina_add_desktop_frontend / "
                "tina_add_web_frontend / tina_add_android_content instead.")
        endif()
    endforeach()

    add_library(${target} STATIC ${ARG_SOURCES})
    target_compile_features(${target} PUBLIC cxx_std_23)
    # PUBLIC so a frontend sees the content's own headers without repeating the path.
    target_include_directories(${target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${target}
        PUBLIC
            Tina::Runtime
            ${ARG_LINK}
    )
    if(ARG_INCLUDE)
        target_include_directories(${target} PUBLIC ${ARG_INCLUDE})
    endif()
    if(ARG_DEFINITIONS)
        target_compile_definitions(${target} PUBLIC ${ARG_DEFINITIONS})
    endif()
    tina_game_project_apply_options(${target})
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
endfunction()

# The portable half of a sample that is not a game. Unlike tina_add_game_content(), this
# deliberately adds no default engine module: a network probe, decoder or benchmark should name
# precisely the engine surface it demonstrates. It still rejects platform targets, because a
# headless sample with an accidental GLFW dependency is just as unportable as game content.
#
#   tina_add_sample_core(sample_network_core SOURCES NetworkScenario.cpp LINK Tina::Network)
function(tina_add_sample_core target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LINK;INCLUDE;DEFINITIONS")
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "tina_add_sample_core(${target}) requires SOURCES.")
    endif()

    foreach(forbidden IN LISTS TINA_GAME_CONTENT_FORBIDDEN_LINKS)
        if(forbidden IN_LIST ARG_LINK)
            message(FATAL_ERROR
                "tina_add_sample_core(${target}) may not link ${forbidden}.\n"
                "Sample logic has to stay portable. Move the platform composition root into "
                "platforms/desktop, platforms/cli, or platforms/web.")
        endif()
    endforeach()

    # Header-only cores get a generated translation unit that includes every header, for two
    # reasons. A STATIC library with nothing to compile produces no .lib, and that surfaces
    # only at link time as an LNK1104 naming a file the frontend never asked for. More
    # importantly, an INTERFACE library would compile the headers nowhere: the frontend
    # compiles them instead, with the platform bootstrap already on the include path, so a
    # core header that reached for GLFW would build fine and the portability claim would hold
    # in name only. Compiling them here is what makes it true.
    set(sources ${ARG_SOURCES})
    set(compilable FALSE)
    foreach(source IN LISTS ARG_SOURCES)
        if(source MATCHES "\\.(cpp|cxx|cc|c)$")
            set(compilable TRUE)
            break()
        endif()
    endforeach()
    if(NOT compilable)
        set(probe "${CMAKE_CURRENT_BINARY_DIR}/${target}_headers.cpp")
        set(probe_text "// Generated by tina_add_sample_core(): compiles this core's headers\n")
        string(APPEND probe_text "// without a platform frontend on the include path.\n")
        foreach(header IN LISTS ARG_SOURCES)
            string(APPEND probe_text "#include \"${CMAKE_CURRENT_SOURCE_DIR}/${header}\"\n")
        endforeach()
        file(GENERATE OUTPUT "${probe}" CONTENT "${probe_text}")
        list(APPEND sources "${probe}")
    endif()

    add_library(${target} STATIC ${sources})
    target_compile_features(${target} PUBLIC cxx_std_23)
    target_include_directories(${target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${target} PUBLIC ${ARG_LINK})
    if(ARG_INCLUDE)
        target_include_directories(${target} PUBLIC ${ARG_INCLUDE})
    endif()
    if(ARG_DEFINITIONS)
        target_compile_definitions(${target} PUBLIC ${ARG_DEFINITIONS})
    endif()
    tina_game_project_apply_options(${target})
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
endfunction()

# A command-line frontend for a portable sample. It intentionally does not add a platform
# bootstrap, copy windowing DLLs, or install a product: headless probes and benchmarks retain
# their existing stdout/exit-code contract while sharing the same directory structure as games.
#
#   tina_add_cli_frontend(tina_sample_network SOURCES Main.cpp CONTENT sample_network_core)
#
# CONTENT is optional here for the same reason as in tina_add_desktop_frontend: a program whose
# whole subject is its own entry point has no portable half worth naming.
function(tina_add_cli_frontend target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "COPY_RUNTIME_DLLS" "CONTENT" "SOURCES;LINK;INCLUDE;DEFINITIONS")
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "tina_add_cli_frontend(${target}) requires SOURCES.")
    endif()

    add_executable(${target} ${ARG_SOURCES})
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_link_libraries(${target} PRIVATE ${ARG_CONTENT} ${ARG_LINK})
    if(ARG_INCLUDE)
        target_include_directories(${target} PRIVATE ${ARG_INCLUDE})
    endif()
    if(ARG_DEFINITIONS)
        target_compile_definitions(${target} PRIVATE ${ARG_DEFINITIONS})
    endif()
    tina_game_project_apply_options(${target})
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)

    if(ARG_COPY_RUNTIME_DLLS AND WIN32)
        # Guarded on a non-empty list: with no DLL dependencies the genex expands to nothing
        # and `cmake -E copy_if_different <dir>` is a usage error, so the POST_BUILD step
        # fails the whole target. The hand-written blocks this helper replaced had the same
        # bug; it only stayed invisible while every such target happened to have a DLL.
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "$<$<BOOL:$<TARGET_RUNTIME_DLLS:${target}>>:${CMAKE_COMMAND}>"
                "$<$<BOOL:$<TARGET_RUNTIME_DLLS:${target}>>:-E;copy_if_different;$<TARGET_RUNTIME_DLLS:${target}>;$<TARGET_FILE_DIR:${target}>>"
            COMMAND_EXPAND_LISTS
        )
    endif()
endfunction()

# The desktop frontend. One executable for Windows, Linux and macOS -- the composition
# root is identical on all three, so there is no per-OS entry file to keep in sync.
#
# Pass INSTALL for a shippable game: that routes staging and install rules through
# tina_install_product() instead of copying DLLs here, so the two never both emit the copy.
#
#   tina_add_desktop_frontend(mygame SOURCES DesktopMain.cpp CONTENT mygame_content INSTALL)
#   tina_add_desktop_frontend(tina_sample_video_probe SOURCES DesktopMain.cpp)
#
# CONTENT is optional, and omitting it is a claim about the sample rather than a shortcut: some
# programs here are nothing but a composition root -- what they demonstrate *is* what
# CreateEngine returns, so there is no portable half to split out. Manufacturing an empty
# content library for those would make the split look enforced exactly where it is not. A game
# always passes CONTENT; if a frontend grows gameplay, move it into core/ and pass it.
function(tina_add_desktop_frontend target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "INSTALL" "CONTENT" "SOURCES;LINK;INCLUDE;DEFINITIONS")
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "tina_add_desktop_frontend(${target}) requires SOURCES.")
    endif()

    add_executable(${target} ${ARG_SOURCES})
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_link_libraries(${target} PRIVATE
        Tina::DesktopBootstrap
        ${ARG_CONTENT}
        ${ARG_LINK}
    )
    if(ARG_INCLUDE)
        target_include_directories(${target} PRIVATE ${ARG_INCLUDE})
    endif()
    if(ARG_DEFINITIONS)
        target_compile_definitions(${target} PRIVATE ${ARG_DEFINITIONS})
    endif()
    tina_game_project_apply_options(${target})
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)

    if(ARG_INSTALL)
        # Emits the install rules and the same DLL copy, keyed to the product directory that
        # tina_product_data_file() stages assets into, so the build tree and an installed
        # copy have one layout.
        tina_install_product(${target})
    elseif(WIN32)
        # Copied into the output directory rather than left to PATH: the sample tree
        # hand-wrote this same block twelve times, and a missing DLL fails at launch with a
        # dialog that names no target.
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_RUNTIME_DLLS:${target}>" "$<TARGET_FILE_DIR:${target}>"
            COMMAND_EXPAND_LISTS
        )
    endif()
endfunction()

# Where a browser build's content lives inside MEMFS.
#
# A browser has no executable directory, so the desktop anchor has no analogue and the root
# has to be a convention instead. It is fixed rather than configurable because two things
# must agree on it -- the link line that packs the files and the frontend that builds the
# ContentRoot -- and a knob is just a way for them to disagree. tina_add_web_frontend()
# therefore compiles it into the frontend as TINA_WEB_CONTENT_ROOT, so the C++ side quotes
# the value instead of retyping it.
set(TINA_WEB_CONTENT_ROOT "/product")

# The browser frontend. Emits a .html because emcc only generates a loader page for that
# suffix; a .js output is something the browser cannot open on its own.
#
#   tina_add_web_frontend(mygame_web SOURCES WebMain.cpp CONTENT mygame_content
#                         SHELL shell.html)
function(tina_add_web_frontend target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "CONTENT;SHELL" "SOURCES;LINK;INCLUDE;DEFINITIONS")
    if(NOT ARG_SOURCES OR NOT ARG_CONTENT)
        message(FATAL_ERROR
            "tina_add_web_frontend(${target}) requires SOURCES and CONTENT.")
    endif()
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR
            "tina_add_web_frontend(${target}) needs the Emscripten toolchain. Guard the "
            "call with if(EMSCRIPTEN), the way a desktop frontend is guarded on its "
            "backend options.")
    endif()

    add_executable(${target} ${ARG_SOURCES})
    target_compile_features(${target} PRIVATE cxx_std_23)
    # No Web CreateEngine helper exists yet, so a browser frontend composes from the
    # platform targets directly. When one lands this list collapses to it, and every
    # caller of this function picks that up without editing.
    target_link_libraries(${target} PRIVATE
        Tina::Platform
        Tina::PlatformHtml5
        Tina::Render
        Tina::Task
        ${ARG_CONTENT}
        ${ARG_LINK}
    )
    if(ARG_INCLUDE)
        target_include_directories(${target} PRIVATE ${ARG_INCLUDE})
    endif()
    # The frontend fills EngineConfig::contentRoot from this, so the packed layout and the
    # resolved layout are one string.
    target_compile_definitions(${target} PRIVATE
        "TINA_WEB_CONTENT_ROOT=\"${TINA_WEB_CONTENT_ROOT}\""
        ${ARG_DEFINITIONS}
    )
    tina_game_project_apply_options(${target})
    set_target_properties(${target} PROPERTIES
        CXX_EXTENSIONS OFF
        SUFFIX ".html"
    )
    target_link_options(${target} PRIVATE
        # The engine allocates well past the default heap, and the frame loop has to
        # survive main() returning -- a browser drives frames from its own callback.
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXIT_RUNTIME=0"
        # Without this an abort inside wasm surfaces as a bare "RuntimeError" with no
        # frames, which is indistinguishable from a hang.
        "-sASSERTIONS=1"
        # Emscripten defaults the stack to 64 KiB, which the bgfx render path overflows on
        # its first frame -- the Opaque3D preflight holds four shadow cascades of matrices
        # by value and its callees nest more frames of the same shape. The overflow looks
        # exactly like a frontend that never started: state never leaves loading and the
        # only evidence is one console line. A desktop target never notices because it
        # gets a megabyte or more by default.
        "-sSTACK_SIZE=1048576"
    )
    if(ARG_SHELL)
        target_link_options(${target} PRIVATE "--shell-file" "${ARG_SHELL}")
        # emcc bakes the shell into the output instead of reading it at load time, so a
        # shell-only edit still has to relink.
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${ARG_SHELL}")
    endif()
endfunction()

# Packs one file into a web frontend's MEMFS at the same relative path
# tina_product_data_file() stages it at on desktop.
#
# That sameness is the point. Both platforms put the file at `relative_destination` below
# their own content root, so the game asks for one string -- contentRoot.resolve("assets/
# game.recipe") -- and never learns which platform answered. Passing a different destination
# per platform is how content ends up with a branch on the frontend it is supposed to be
# independent of.
#
#   tina_web_data_file(mygame_web "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/game.recipe"
#                      "assets/game.recipe")
#
# The file is packed at link time into the .data file beside the .html, not fetched at
# runtime, and Emscripten holds main() until the pack has been unpacked into MEMFS -- so
# content that reads during startup finds it. There is no install rule to pair this with:
# a browser build's deliverable is the emitted .html/.js/.wasm/.data set itself.
function(tina_web_data_file target source relative_destination)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR
            "tina_web_data_file(${target}) needs the Emscripten toolchain. Guard the call "
            "with if(EMSCRIPTEN).")
    endif()
    if(NOT EXISTS "${source}")
        message(FATAL_ERROR
            "tina_web_data_file(${target}): source does not exist: ${source}")
    endif()
    # These are rejected here rather than left to fail in a browser, because Core::ContentRoot
    # refuses the same shapes at resolve() time and a packed-but-unreachable file is the one
    # failure that looks like a missing asset instead of a wrong argument.
    if(relative_destination STREQUAL "")
        message(FATAL_ERROR "tina_web_data_file(${target}): destination is empty.")
    endif()
    if(relative_destination MATCHES "^/")
        message(FATAL_ERROR
            "tina_web_data_file(${target}): destination must be relative to the content "
            "root, not absolute: ${relative_destination}")
    endif()
    if(relative_destination MATCHES "\\\\")
        message(FATAL_ERROR
            "tina_web_data_file(${target}): destination must use '/' separators: "
            "${relative_destination}")
    endif()
    if(relative_destination MATCHES "(^|/)\\.\\.?(/|$)")
        message(FATAL_ERROR
            "tina_web_data_file(${target}): destination must not contain a '.' or '..' "
            "component: ${relative_destination}")
    endif()

    # The '=' form, not a "--preload-file" "value" pair: CMake de-duplicates link options,
    # which would collapse two pairs into one flag with two values and silently drop a file.
    target_link_options(${target} PRIVATE
        "--preload-file=${source}@${TINA_WEB_CONTENT_ROOT}/${relative_destination}"
    )
    # emcc reads the file while linking, so a changed asset needs a relink to reach the
    # .data file. Without this the browser keeps loading the previous contents.
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${source}")
endfunction()

# Locates the asset cooker and returns its path in out_var.
#
# Three sources, in order: an explicit TINA_ASSETC_EXECUTABLE override, the in-tree target
# when this project is a Tina checkout, and the installed SDK's bin directory. The override
# comes first because it is the only answer when cross-compiling -- the cooker runs on the
# build host, so a target-architecture build of it cannot execute, the same reason
# TINA_BGFX_SHADERC_EXECUTABLE exists for shaderc.
#
# Sets out_var to a path, or to a NOTFOUND value that names the reason. It does not fail:
# a project can legitimately choose to cook at startup instead, and only the caller knows
# whether the cooker is required.
function(tina_find_assetc out_var)
    if(TINA_ASSETC_EXECUTABLE)
        set(${out_var} "${TINA_ASSETC_EXECUTABLE}" PARENT_SCOPE)
        return()
    endif()
    if(TARGET tina_assetc)
        # A generator expression, so this survives a multi-config generator where the path is
        # not known until build time.
        set(${out_var} "$<TARGET_FILE:tina_assetc>" PARENT_SCOPE)
        return()
    endif()
    if(CMAKE_CROSSCOMPILING)
        set(${out_var} "TINA_ASSETC-NOTFOUND-CROSSCOMPILING" PARENT_SCOPE)
        return()
    endif()
    if(DEFINED Tina_WITH_AssetC AND NOT Tina_WITH_AssetC)
        # The package is intact; it was simply built without tools/. Distinguished from a
        # plain not-found so this reads as "rebuild the SDK with TINA_BUILD_TOOLS=ON"
        # rather than "the install is broken".
        set(${out_var} "TINA_ASSETC-NOTFOUND-NOT-PACKAGED" PARENT_SCOPE)
        return()
    endif()
    find_program(TINA_ASSETC_EXECUTABLE
        NAMES tina_assetc
        HINTS "${Tina_ASSETC_HINT_DIR}"
        DOC "Path to the Tina asset cooker (tina_assetc)"
    )
    if(TINA_ASSETC_EXECUTABLE)
        set(${out_var} "${TINA_ASSETC_EXECUTABLE}" PARENT_SCOPE)
    else()
        set(${out_var} "TINA_ASSETC-NOTFOUND" PARENT_SCOPE)
    endif()
endfunction()

# Cooks a recipe into a catalog at build time and stages the result beside `target`.
#
# This is the step that lets a product stop cooking at startup. The catalog lands at
# `relative_destination` below the product's content root, which is the path the game passes
# to ContentRoot::resolve(), so moving from startup cooking to build-time cooking is a
# CMakeLists change with no C++ edit.
#
# Requires the cooker. Callers that cannot guarantee one -- a cross-compile, or an SDK built
# without tools/ -- should check tina_find_assetc() first and keep the startup path.
#
#   tina_cook_catalog(mygame RECIPE "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/game.recipe"
#                     DESTINATION "content")
function(tina_cook_catalog target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "RECIPE;DESTINATION" "DEPENDS")
    if(NOT ARG_RECIPE OR NOT ARG_DESTINATION)
        message(FATAL_ERROR "tina_cook_catalog(${target}) requires RECIPE and DESTINATION.")
    endif()
    if(NOT EXISTS "${ARG_RECIPE}")
        message(FATAL_ERROR "tina_cook_catalog(${target}): recipe does not exist: ${ARG_RECIPE}")
    endif()

    tina_find_assetc(assetc)
    if(assetc MATCHES "NOTFOUND")
        message(FATAL_ERROR
            "tina_cook_catalog(${target}) needs the asset cooker, and none was found "
            "(${assetc}).\n"
            "Set TINA_ASSETC_EXECUTABLE to a host build of tina_assetc, or cook at startup "
            "instead -- see openContent() in templates/game-project/core/GameApplication.cpp.")
    endif()

    set(catalog_root "$<TARGET_FILE_DIR:${target}>/${ARG_DESTINATION}")
    # POST_BUILD rather than a separate custom target with an OUTPUT: the cooker writes a
    # directory tree whose file names come out of the recipe, so there is no output list to
    # declare. DEPENDS carries the payload files the recipe reads, which is what makes an edit
    # to one of them re-cook.
    #
    # The catalog directory is removed first, not created: the cooker creates --out itself and
    # fails with "staging root already exists" if it is there, so a rebuild would break on the
    # output of the previous one. Its parent has to exist though, which for a nested
    # destination is not guaranteed.
    cmake_path(GET ARG_DESTINATION PARENT_PATH destination_parent)
    if(destination_parent STREQUAL "")
        set(catalog_parent "$<TARGET_FILE_DIR:${target}>")
    else()
        set(catalog_parent "$<TARGET_FILE_DIR:${target}>/${destination_parent}")
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${catalog_root}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${catalog_parent}"
        COMMAND "${assetc}" --out "${catalog_root}" --recipe "${ARG_RECIPE}"
        DEPENDS "${ARG_RECIPE}" ${ARG_DEPENDS}
        COMMENT "Cooking ${ARG_DESTINATION} for ${target}"
        VERBATIM
    )
endfunction()

# There is deliberately no tina_add_android_frontend().
#
# Java resolves System.loadLibrary("tina_android") against one fixed name
# (TinaNative.java:17), so Android has no per-game shared library to produce: a game
# reaches the device by having the engine's JNI bridge link its content library, the way
# Tina::SampleGallery is linked today. Until that bridge takes its application from a
# factory the game defines, an Android frontend function would only wrap an edit to engine
# source, so this file offers none rather than implying the seam exists.
#
# Asset loading is no longer a gap, and it is not solved from here. android/app/build.gradle
# cooks with a host tina_assetc (-Ptina.assetc=<path>) into a generated assets source set, so
# the APK carries assets/content/, and TinaActivity extracts the tree to getFilesDir()/content
# and hands that to EngineConfig::contentRoot. tina_cook_catalog() is deliberately not reused:
# its destination is $<TARGET_FILE_DIR:target>, which on Android is the NDK .so output
# directory, and AGP harvests only .so from there. Ordering also has to be declared against
# mergeAssets, which a CMake POST_BUILD inside externalNativeBuild cannot do.
#
# Two things that sound like blockers and are not. TargetPlatform has no Android value, but
# `Any` exists and no loader validates the field -- it is read only by cook-side code -- so a
# device catalog names Any and loads unchanged. AAssetManager is still never called anywhere,
# on purpose: extracting once keeps Core::readFile as the single read path on every platform.

# Product-local catalog directory for samples that still cook at startup.
#
# Always `$<TARGET_FILE_DIR:target>/content`, never the system temporary directory: this
# host's AppData temp tree cannot create AF_UNIX sockets, and a catalog that lands in
# `%TEMP%` also vanishes independently of the build tree. C++ resolves the same path as
# `applicationDirectory() / "content"` (or `contentRoot.resolve("content")` once the
# frontend has filled EngineConfig). The helper exists so CMake and C++ cannot drift.
function(tina_sample_runtime_cook_dir target out_var)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "tina_sample_runtime_cook_dir(${target}): target does not exist.")
    endif()
    set(${out_var} "$<TARGET_FILE_DIR:${target}>/content" PARENT_SCOPE)
endfunction()

# Fails if any platform target is reachable from `target`'s transitive link closure.
#
# The LINK check inside tina_add_game_content only sees what that call passed. Content that
# links a helper library which itself links GLFW is just as unportable and passes that
# check, so this walks the whole graph. Call it on a content library once the frontends
# are defined.
function(tina_verify_game_content_portable target)
    set(pending "${target}")
    set(visited "")
    while(pending)
        list(POP_FRONT pending current)
        if(current IN_LIST visited)
            continue()
        endif()
        list(APPEND visited "${current}")

        if(current IN_LIST TINA_GAME_CONTENT_FORBIDDEN_LINKS)
            message(FATAL_ERROR
                "Game content ${target} reaches the platform target ${current} through its "
                "link graph, so it is buildable for one platform only.\n"
                "Visited: ${visited}\n"
                "Find the library on that path that links ${current} and move the platform "
                "dependency into a frontend.")
        endif()

        # Generator expressions and raw linker flags are not targets; skipping them keeps
        # this from tripping over things like "-sASSERTIONS=1".
        if(NOT TARGET ${current})
            continue()
        endif()
        foreach(property INTERFACE_LINK_LIBRARIES LINK_LIBRARIES)
            get_target_property(dependencies ${current} ${property})
            if(dependencies)
                foreach(dependency IN LISTS dependencies)
                    if(NOT dependency MATCHES "^\\$<")
                        list(APPEND pending "${dependency}")
                    endif()
                endforeach()
            endif()
        endforeach()
    endwhile()
endfunction()
