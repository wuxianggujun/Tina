# Product 2D sample (M10-A32-A36): Catalog TileMap + Character + optional Physics2D/FreeType.
# Target name is the formal tina_sample_2d product gate; the directory keeps its tilemap history.
tina_add_desktop_frontend(tina_sample_2d
    SOURCES DesktopMain.cpp
    CONTENT tina_sample_2d_core
    LINK
        Tina::Asset
        Tina::Navigation2D
        Tina::Scene
        Tina::RenderBgfx
    INCLUDE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/samples/common
    INSTALL
)
if (TINA_BUILD_PHYSICS2D)
    target_compile_definitions(tina_sample_2d PRIVATE TINA_SAMPLE_TILEMAP_PHYSICS2D=1)
    target_link_libraries(tina_sample_2d PRIVATE Tina::Physics2D)
endif ()
if (TINA_BUILD_UI_FREETYPE)
    target_compile_definitions(tina_sample_2d PRIVATE TINA_SAMPLE_TILEMAP_FREETYPE=1)
    target_link_libraries(tina_sample_2d PRIVATE Tina::UIFreetype)
    tina_target_stage_ui_font(tina_sample_2d)
endif ()
# Optional miniaudio null-device SFX path (M11-A16): attachMixer + start so mixRealtime runs
# from the device callback. The default product-2d preset enables this with physics/freetype.
if (TINA_BUILD_AUDIO_MINIAUDIO)
    target_compile_definitions(tina_sample_2d PRIVATE TINA_SAMPLE_TILEMAP_AUDIO_MINIAUDIO=1)
    target_link_libraries(tina_sample_2d PRIVATE Tina::AudioMiniaudio)
endif ()

# The recipe ships beside the executable and is resolved relative to it at runtime, so the build
# tree and an installed copy use one code path. Compiling the source-tree path in instead made
# the sample unrunnable anywhere but the machine that built it.
tina_product_data_file(tina_sample_2d
    "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/sample_2d.recipe"
    "assets/sample_2d.recipe"
)
