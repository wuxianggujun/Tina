tina_add_sample_core(tina_sample_3d_custom_shader_core
    SOURCES
        main.cpp
        Sample.hpp
    LINK
        Tina::Runtime
        # The shader payload reaches the device through Asset::uploadShaderFromCooked; the
        # skinned mesh is built in memory and uploaded through uploadSkinnedMeshFromCooked.
        Tina::Asset
)
