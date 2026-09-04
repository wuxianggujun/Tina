# Terraria-like 2D sample: procedurally generated destructible tile world.
#
# The world is generated at runtime into recipe text and cooked in-process, so the shipped
# .recipe holds only the static assets (atlas + tileset + tile table). That split exists because
# the recipe cooker skips all-empty chunks, which would make every sky cell permanently
# unbuildable -- the generator emits an explicit air tile instead of tile id 0.
tina_add_desktop_frontend(tina_sample_terraria
    SOURCES DesktopMain.cpp
    CONTENT tina_sample_terraria_core
    LINK
        Tina::Asset
        Tina::Scene
        Tina::RenderBgfx
    INCLUDE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/samples/common
    INSTALL
)
if (TINA_BUILD_UI_FREETYPE)
    target_compile_definitions(tina_sample_terraria PRIVATE TINA_SAMPLE_TERRARIA_FREETYPE=1)
    target_link_libraries(tina_sample_terraria PRIVATE Tina::UIFreetype)
    tina_target_stage_ui_font(tina_sample_terraria)
endif ()

# Resolved relative to the executable at runtime, so the build tree and an installed copy share
# one code path.
tina_product_data_file(tina_sample_terraria
    "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/world_assets.recipe"
    "assets/world_assets.recipe"
)
