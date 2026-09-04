# The voxel world itself: storage, meshing, raycast, block atlas. This is the half worth having
# portable -- it is the sample's whole subject and touches no window.
#
# No Tina::Asset and no staged data file. The block atlas is generated in-process by
# BlockAtlas.hpp rather than cooked: a recipe would drag in the cooker, a catalog directory and a
# runtime catalog load just to carry 4 KB of flat-colour pixels.
tina_add_sample_core(tina_sample_3d_voxel_core
    SOURCES
        BlockAtlas.hpp
        ChunkMesh.hpp
        DeviceCapture.hpp
        VoxelRaycast.hpp
        VoxelWorld.hpp
    INCLUDE
        ${PROJECT_SOURCE_DIR}/samples/common
    LINK
        Tina::Runtime
        Tina::Render
)
