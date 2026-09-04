# UI, the environment-map fixture and the capture decorator. The fixture is portable on purpose:
# it synthesises its cubemap in memory rather than reading a file, so it needs no content root.
tina_add_sample_core(tina_sample_3d_core
    SOURCES
        DeviceCapture.hpp
        Product3DUI.cpp
        Product3DUI.hpp
        ProductEnvironmentMapFixture.cpp
        ProductEnvironmentMapFixture.hpp
    INCLUDE
        ${PROJECT_SOURCE_DIR}/samples/common
    LINK
        Tina::Runtime
        Tina::Asset
        Tina::Render
)
