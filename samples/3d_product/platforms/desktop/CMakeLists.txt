tina_add_desktop_frontend(tina_sample_3d
    SOURCES DesktopMain.cpp
    CONTENT tina_sample_3d_core
    LINK
        Tina::Scene
        Tina::RenderBgfx
    INCLUDE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/samples/common
    INSTALL
)

# The default product gate uses the complete PBR glTF that lives in this sample's assets/.
# It ships beside the executable and is resolved relative to it, same rule as the 2D recipe:
# compiling a source-tree path in named a file that exists only on the build machine.
# The .gltf's four URIs are relative, so the whole set has to land in one directory for them
# to keep resolving. GltfCookTests reads the same files from here rather than a second copy.
set(_tina_complete_pbr_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/complete_pbr")
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
