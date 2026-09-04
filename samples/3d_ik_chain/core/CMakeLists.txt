tina_add_sample_core(tina_sample_3d_ik_chain_core
    SOURCES
        main.cpp
        Sample.hpp
    LINK
        Tina::Runtime
        # Skeleton3D and solveTwoBoneIk. This is the first sample to link the module at all.
        Tina::Animation3D
        # writeSkinnedMeshPayloadBytes builds the cooked payload in memory, and
        # uploadSkinnedMeshFromCooked hands it to the device without a catalog.
        Tina::Asset
)
