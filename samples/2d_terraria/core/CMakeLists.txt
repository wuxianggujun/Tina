# World generation and the capture decorator. WorldGen.hpp is the reason this half exists: the
# generator is the sample's actual subject and has nothing to do with the window it draws in.
tina_add_sample_core(tina_sample_terraria_core
    SOURCES
        DeviceCapture.hpp
        WorldGen.hpp
    LINK
        Tina::Runtime
        Tina::Render
)
