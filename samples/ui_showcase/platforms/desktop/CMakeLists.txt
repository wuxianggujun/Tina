tina_add_desktop_frontend(tina_sample_ui_showcase
    SOURCES DesktopMain.cpp
    CONTENT tina_sample_ui_showcase_core
)

if(TINA_BUILD_UI_FREETYPE)
    tina_target_stage_ui_font(tina_sample_ui_showcase)
endif()
