# One executable for Windows, Linux and macOS: the composition root is identical on all
# three, so there is no per-OS entry file to keep in sync.
#
# INSTALL makes this a shippable product -- it emits the install rules and, on Windows,
# stages the runtime DLLs beside the binary.
tina_add_desktop_frontend(mygame
    SOURCES main.cpp
    CONTENT mygame_content
    INSTALL
)

# Only cooked content is shipped. A failed cook preserves the last good catalog.
tina_cook_catalog(mygame
    RECIPE "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/game.recipe"
    DESTINATION "content"
)
