add_executable(tina_sample_2d_tilemap_bgfx
    main.cpp
)

target_compile_features(tina_sample_2d_tilemap_bgfx PRIVATE cxx_std_23)
target_include_directories(tina_sample_2d_tilemap_bgfx PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(tina_sample_2d_tilemap_bgfx PRIVATE
    Tina::ProjectOptions
    Tina::DesktopBootstrap
    Tina::Runtime
    Tina::Asset
    Tina::RenderBgfx
)
if (TINA_BUILD_PHYSICS2D)
    target_compile_definitions(tina_sample_2d_tilemap_bgfx PRIVATE TINA_SAMPLE_TILEMAP_PHYSICS2D=1)
    target_link_libraries(tina_sample_2d_tilemap_bgfx PRIVATE Tina::Physics2D)
endif ()
set_target_properties(tina_sample_2d_tilemap_bgfx PROPERTIES CXX_EXTENSIONS OFF)

if(WIN32)
    add_custom_command(TARGET tina_sample_2d_tilemap_bgfx POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:tina_sample_2d_tilemap_bgfx>
            $<TARGET_FILE_DIR:tina_sample_2d_tilemap_bgfx>
        COMMAND_EXPAND_LISTS
    )
endif()
