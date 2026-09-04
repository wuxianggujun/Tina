# No cook step: the skinned mesh payload is built in memory by core/, so this frontend only
# supplies main(). Nothing about the sample's content is platform-specific.
tina_add_desktop_frontend(tina_sample_3d_ik_chain
    SOURCES
        DesktopMain.cpp
    CONTENT tina_sample_3d_ik_chain_core
)
