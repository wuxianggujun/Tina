# Composition root only: this sample's subject is the catalog load path it wires up, so it has
# no portable half to put in core/.
tina_add_desktop_frontend(tina_sample_2d_catalog
    SOURCES DesktopMain.cpp
    LINK
        Tina::Asset
        Tina::RenderBgfx
    INCLUDE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/samples/common
)
