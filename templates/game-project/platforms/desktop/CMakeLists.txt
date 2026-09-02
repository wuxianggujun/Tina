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

# Assets are staged by the *frontend*, not by core/, because the destination is a platform
# decision: a desktop game resolves files next to its executable, a browser build has them
# packed into MEMFS by tina_web_data_file(), and an Android build reads them out of the APK.
# core/ therefore never names a directory -- it only names the relative path below whichever
# content root the frontend put in EngineConfig. See main.cpp.
#
# The recipe lands at the same relative path in the build tree and in an installed copy, so
# contentRoot.resolve("assets/game.recipe") is correct in both. Staging one half by hand is
# how a file comes to sit beside a binary yet be missing from every install.
tina_product_data_file(mygame
    "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/game.recipe"
    "assets/game.recipe"
)

# To cook at build time instead of at startup, add:
#
#   tina_cook_catalog(mygame
#       RECIPE "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/game.recipe"
#       DESTINATION "content")
#
# DESTINATION is the same string core/ passes to contentRoot.resolve(), so this is a
# CMakeLists-only change apart from deleting the cook half of openContent(). It requires the
# cooker, which a cross-compile and an SDK built without tools/ do not have -- see README.md.
