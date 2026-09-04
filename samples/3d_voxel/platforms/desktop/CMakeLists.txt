# 3D voxel sample: first-person Minecraft-like core loop.
#
# Walk/jump with WASD/Space, look with mouse (cursor locked), break blocks with LMB, place blocks
# with RMB, switch block type with number keys 1-4. The world is a fixed grid of chunks meshed on
# demand; no persistence, no monsters, no crafting.
tina_add_desktop_frontend(tina_sample_3d_voxel
    SOURCES DesktopMain.cpp
    CONTENT tina_sample_3d_voxel_core
    LINK
        Tina::RenderBgfx
    INCLUDE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/samples/common
    INSTALL
)
