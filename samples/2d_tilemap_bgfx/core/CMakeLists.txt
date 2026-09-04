# The portable half: tile selection, audio state, frame pacing and the capture decorator. None
# of these name a platform type, which is what lets them be compiled once and linked by any
# frontend.
tina_add_sample_core(tina_sample_2d_core
    SOURCES
        AudioControlState.hpp
        DeviceCapture.hpp
        TileSelection.hpp
        WindowVisibilityPacing.hpp
    LINK
        Tina::Runtime
        Tina::Render
)
