# No cook step: both the skinned mesh and the three animation clips are built in memory by
# core/, so this frontend only supplies main(). Nothing about the sample's content is
# platform-specific.
tina_add_desktop_frontend(tina_sample_3d_animation_graph
    SOURCES
        DesktopMain.cpp
    CONTENT tina_sample_3d_animation_graph_core
)
