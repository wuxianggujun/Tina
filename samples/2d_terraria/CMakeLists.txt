# Terraria-like 2D sample: procedurally generated destructible tile world.
#
# The world is generated at runtime into recipe text and cooked in-process, so the
# shipped .recipe holds only the static assets (atlas + tileset + tile table). That
# split exists because the recipe cooker skips all-empty chunks, which would make
# every sky cell permanently unbuildable -- the generator emits an explicit air tile
# instead of tile id 0.
add_executable(tina_sample_terraria
    main.cpp
)

target_compile_features(tina_sample_terraria PRIVATE cxx_std_23)
target_include_directories(tina_sample_terraria PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/samples/common"
)
target_link_libraries(tina_sample_terraria PRIVATE
    Tina::ProjectOptions
    Tina::DesktopBootstrap
    Tina::Runtime
    Tina::Asset
    Tina::Scene
    Tina::RenderBgfx
)
if (TINA_BUILD_UI_FREETYPE)
    target_compile_definitions(tina_sample_terraria PRIVATE TINA_SAMPLE_TERRARIA_FREETYPE=1)
    target_link_libraries(tina_sample_terraria PRIVATE Tina::UIFreetype)
    tina_target_stage_ui_font(tina_sample_terraria)
endif ()
set_target_properties(tina_sample_terraria PROPERTIES CXX_EXTENSIONS OFF)

# Resolved relative to the executable at runtime, so the build tree and an installed
# copy share one code path.
tina_product_data_file(tina_sample_terraria
    "${CMAKE_CURRENT_SOURCE_DIR}/catalog/world_assets.recipe"
    "catalog/world_assets.recipe"
)
tina_install_product(tina_sample_terraria)
