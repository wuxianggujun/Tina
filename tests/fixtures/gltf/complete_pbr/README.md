# complete_pbr glTF fixture

Product/cook regression fixture for Tina 3D:

- 2 meshes / 2 materials / 2 scene nodes (Right translated +X)
- POSITION + NORMAL + TEXCOORD_0 per mesh; Cooker generates MikkTSpace tangents
- Shared 1x1 PNG decode fixtures: white base color, full G/B metallic-roughness,
  and a flat `(128, 128, 255)` tangent-space normal
- Distinct metallic/roughness factors (dielectric vs metal)

Used by:

- `tina_sample_3d` default gate when `TINA_COMPLETE_PBR_GLTF_FIXTURE` is defined
- `GltfCookTests.CooksRepoCompletePbrFixture`

Not a production art pack. Games should ship their own cooked Catalog assets.
