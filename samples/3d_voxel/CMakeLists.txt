# 3D voxel sample: first-person Minecraft-like core loop.
#
# Walk/jump with WASD/Space, look with mouse (cursor locked), break blocks with LMB,
# place blocks with RMB, switch block type with number keys 1-4. The world is a fixed
# grid of chunks meshed on demand; no persistence, no monsters, no crafting.
add_executable(tina_sample_3d_voxel
    main.cpp
)

target_compile_features(tina_sample_3d_voxel PRIVATE cxx_std_23)
target_include_directories(tina_sample_3d_voxel PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/samples/common"
)
# No Tina::Asset and no staged data file. The block atlas is generated in-process by
# BlockAtlas.hpp rather than cooked: a recipe would drag in the cooker, a temp catalog
# directory and a runtime catalog load just to carry 4 KB of flat-colour pixels.
target_link_libraries(tina_sample_3d_voxel PRIVATE
    Tina::ProjectOptions
    Tina::DesktopBootstrap
    Tina::Runtime
    Tina::RenderBgfx
)
set_target_properties(tina_sample_3d_voxel PROPERTIES CXX_EXTENSIONS OFF)

tina_install_product(tina_sample_3d_voxel)
