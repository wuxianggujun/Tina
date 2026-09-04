tina_add_sample_core(tina_sample_2d_shader_materials_core
    SOURCES
        main.cpp
        Sample.hpp
    LINK
        Tina::Runtime
        # The shader payload reaches the device through Asset::uploadShaderFromCooked, which is
        # what maps the AssetFormat shader vocabulary onto the Render one.
        Tina::Asset
)
