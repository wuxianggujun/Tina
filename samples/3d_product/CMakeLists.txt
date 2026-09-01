add_executable(tina_sample_3d
    main.cpp
    DeviceCapture.hpp
    Product3DUI.cpp
    Product3DUI.hpp
    ProductEnvironmentMapFixture.cpp
    ProductEnvironmentMapFixture.hpp
)

target_compile_features(tina_sample_3d PRIVATE cxx_std_23)
target_include_directories(tina_sample_3d PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/samples/common"
)
target_link_libraries(tina_sample_3d PRIVATE
    Tina::ProjectOptions
    Tina::DesktopBootstrap
    Tina::Runtime
    Tina::Asset
    Tina::Scene
    Tina::RenderBgfx
)
set_target_properties(tina_sample_3d PROPERTIES CXX_EXTENSIONS OFF)

# Default product gate uses the complete PBR repo fixture (multi-mesh + MR/normal maps).
# It ships beside the executable and is resolved relative to it, same rule as the 2D
# recipe: the source-tree path this used to compile in named a file that exists only on
# the build machine. The .gltf's four URIs are relative, so the whole set has to land in
# one directory for them to keep resolving.
set(_tina_complete_pbr_dir "${PROJECT_SOURCE_DIR}/tests/fixtures/gltf/complete_pbr")
foreach(_fixture_file IN ITEMS
        complete_pbr.gltf
        geometry.bin
        base_color.png
        metallic_roughness.png
        normal.png)
    tina_product_data_file(tina_sample_3d
        "${_tina_complete_pbr_dir}/${_fixture_file}"
        "assets/complete_pbr/${_fixture_file}"
    )
endforeach()

tina_install_product(tina_sample_3d)
