# complete_pbr glTF

This sample's author source for the default `tina_sample_3d` product gate:

- 2 meshes / 2 materials / 2 scene nodes (Right translated +X)
- POSITION + NORMAL + TEXCOORD_0 per mesh; Cooker generates MikkTSpace tangents
- Shared 1x1 PNG decode fixtures: white base color, full G/B metallic-roughness,
  and a flat `(128, 128, 255)` tangent-space normal
- Distinct metallic/roughness factors (dielectric vs metal)

The desktop frontend stages the whole set at `assets/complete_pbr/` beside the
executable via `tina_product_data_file()`. `GltfCookTests.CooksRepoCompletePbrFixture`
reads the same files from here (`TINA_COMPLETE_PBR_GLTF_FIXTURE`); there is no
second copy under `tests/fixtures/`.

Not a production art pack. Games should ship their own cooked Catalog assets.
