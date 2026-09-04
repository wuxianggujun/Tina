# Browser composition root. Drives EngineHost::tick from emscripten_set_main_loop, because a
# browser owns the frame loop and run() would have to block.
#
# No CONTENT: this sample's subject is that composition root. The bgfx device factory is a
# private header, reached the same way the desktop samples reach it rather than by publishing it
# for one sample.
tina_add_web_frontend(tina_sample_web
    SOURCES WebMain.cpp
    SHELL "${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
    LINK
        Tina::Runtime
        Tina::RenderBgfx
    INCLUDE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/samples/common
)
