add_executable(tina_sample_web
    main.cpp
)

target_compile_features(tina_sample_web PRIVATE cxx_std_23)
# The bgfx device factory is a private header, reached the same way the desktop samples
# reach it rather than by publishing it just for this sample.
target_include_directories(tina_sample_web PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(tina_sample_web
    PRIVATE
        Tina::ProjectOptions
        Tina::Runtime
        Tina::Platform
        Tina::PlatformHtml5
        Tina::Task
        Tina::Render
        Tina::RenderBgfx
)

# .html output makes emcc generate the loader page from our shell; without SUFFIX it
# would emit a .js the browser cannot open on its own.
set_target_properties(tina_sample_web PROPERTIES
    CXX_EXTENSIONS OFF
    SUFFIX ".html"
)

set(tina_sample_web_shell "${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
target_link_options(tina_sample_web
    PRIVATE
        "--shell-file" "${tina_sample_web_shell}"
        # The engine allocates well past the default heap, and the loop must survive
        # main() returning.
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXIT_RUNTIME=0"
        # Without this an abort inside wasm gives a bare "RuntimeError" with no frames,
        # which is indistinguishable from a hang in a browser.
        "-sASSERTIONS=1"
        # Emscripten defaults the stack to 64 KiB, which the bgfx render path overflows on
        # its first frame: the Opaque3D preflight holds four shadow cascades of matrices by
        # value, and its callees nest several more frames of the same shape. A desktop
        # target never notices because it gets a megabyte or more by default.
        "-sSTACK_SIZE=1048576"
)
# Relink when only the shell changes; emcc bakes it into the output rather than reading
# it at load time.
set_property(TARGET tina_sample_web APPEND PROPERTY LINK_DEPENDS "${tina_sample_web_shell}")
