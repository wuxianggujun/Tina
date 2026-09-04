tina_add_sample_core(tina_sample_3d_animation_graph_core
    SOURCES
        main.cpp
        Sample.hpp
    LINK
        Tina::Runtime
        # AnimationGraph3D, BlendTree3D, ClipSampler3D. 3d_ik_chain links this module for the IK
        # solver alone; nothing yet exercised the clip/blend/state-machine half of it.
        Tina::Animation3D
        # Builds the cooked skinned-mesh and clip payloads in memory, then uploads the mesh
        # without a catalog.
        Tina::Asset
)
