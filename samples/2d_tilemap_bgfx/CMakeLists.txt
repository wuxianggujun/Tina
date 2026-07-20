# Product 2D sample (M10-A32–A36): Catalog TileMap + Character + optional Physics2D/FreeType.
# Target name is the formal tina_sample_2d product gate; directory keeps tilemap_bgfx history.
add_executable(tina_sample_2d
    main.cpp
)
# Backward-compatible target name for scripts still using the intermediate name.
add_executable(tina_sample_2d_tilemap_bgfx ALIAS tina_sample_2d)

target_compile_features(tina_sample_2d PRIVATE cxx_std_23)
target_include_directories(tina_sample_2d PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(tina_sample_2d PRIVATE
    Tina::ProjectOptions
    Tina::DesktopBootstrap
    Tina::Runtime
    Tina::Asset
    Tina::RenderBgfx
)
if (TINA_BUILD_PHYSICS2D)
    target_compile_definitions(tina_sample_2d PRIVATE TINA_SAMPLE_TILEMAP_PHYSICS2D=1)
    target_link_libraries(tina_sample_2d PRIVATE Tina::Physics2D)
endif ()
if (TINA_BUILD_UI_FREETYPE)
    target_compile_definitions(tina_sample_2d PRIVATE
        TINA_SAMPLE_TILEMAP_FREETYPE=1
        TINA_SAMPLE_TILEMAP_FONT_PATH="${PROJECT_SOURCE_DIR}/resources/fonts/SourceHanSansSC-Regular.otf"
    )
    target_link_libraries(tina_sample_2d PRIVATE Tina::UIFreetype)
endif ()
set_target_properties(tina_sample_2d PROPERTIES CXX_EXTENSIONS OFF)

if(WIN32)
    add_custom_command(TARGET tina_sample_2d POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:tina_sample_2d>
            $<TARGET_FILE_DIR:tina_sample_2d>
        COMMAND_EXPAND_LISTS
    )
endif()
