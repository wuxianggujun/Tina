# Product 2D sample (M10-A32–A36): Catalog TileMap + Character + optional Physics2D/FreeType.
# Target name is the formal tina_sample_2d product gate; directory keeps tilemap_bgfx history.
add_executable(tina_sample_2d
    main.cpp
)

target_compile_features(tina_sample_2d PRIVATE cxx_std_23)
target_include_directories(tina_sample_2d PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/samples/common"
)
target_link_libraries(tina_sample_2d PRIVATE
    Tina::ProjectOptions
    Tina::DesktopBootstrap
    Tina::Runtime
    Tina::Asset
    Tina::Navigation2D
    Tina::Scene
    Tina::RenderBgfx
)
if (TINA_BUILD_PHYSICS2D)
    target_compile_definitions(tina_sample_2d PRIVATE TINA_SAMPLE_TILEMAP_PHYSICS2D=1)
    target_link_libraries(tina_sample_2d PRIVATE Tina::Physics2D)
endif ()
if (TINA_BUILD_UI_FREETYPE)
    target_compile_definitions(tina_sample_2d PRIVATE TINA_SAMPLE_TILEMAP_FREETYPE=1)
    tina_target_optional_ui_font(tina_sample_2d TINA_SAMPLE_TILEMAP_FONT_PATH)
    target_link_libraries(tina_sample_2d PRIVATE Tina::UIFreetype)
endif ()
# Optional miniaudio null-device SFX path (M11-A16): attachMixer + start so mixRealtime
# runs from the device callback. Default product-2d enables this with physics/freetype.
if (TINA_BUILD_AUDIO_MINIAUDIO)
    target_compile_definitions(tina_sample_2d PRIVATE TINA_SAMPLE_TILEMAP_AUDIO_MINIAUDIO=1)
    target_link_libraries(tina_sample_2d PRIVATE Tina::AudioMiniaudio)
endif ()
# Product catalog recipe (disk file cook path; not in-process payload assembly).
target_compile_definitions(tina_sample_2d PRIVATE
    TINA_SAMPLE_2D_RECIPE_PATH="${PROJECT_SOURCE_DIR}/samples/2d_tilemap_bgfx/catalog/sample_2d.recipe"
)
set_target_properties(tina_sample_2d PROPERTIES CXX_EXTENSIONS OFF)

if(WIN32)
    add_custom_command(TARGET tina_sample_2d POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:tina_sample_2d>
            $<TARGET_FILE_DIR:tina_sample_2d>
        COMMAND_EXPAND_LISTS
    )
endif()
