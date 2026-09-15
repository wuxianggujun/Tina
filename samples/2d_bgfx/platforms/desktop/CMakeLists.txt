tina_add_desktop_frontend(tina_sample_2d_infrastructure_bgfx
    SOURCES DesktopMain.cpp
    INCLUDE
        ${PROJECT_SOURCE_DIR}/samples/common
)
if (TINA_BUILD_AUDIO_MINIAUDIO)
    target_compile_definitions(tina_sample_2d_infrastructure_bgfx PRIVATE TINA_SAMPLE_2D_BGFX_AUDIO_MINIAUDIO=1)
    target_link_libraries(tina_sample_2d_infrastructure_bgfx PRIVATE Tina::GameSDK)
endif ()
