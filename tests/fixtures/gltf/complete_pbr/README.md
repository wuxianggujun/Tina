# complete_pbr glTF fixture

Product/cook regression fixture for Tina 3D:

- 2 meshes / 2 materials / 2 scene nodes (Right translated +X)
- POSITION + NORMAL + TEXCOORD_0 per mesh
- Shared baseColor / metallicRoughness / normal PNG images (1×1 decode fixtures)
- Distinct metallic/roughness factors (dielectric vs metal)

Used by:

- `tina_sample_3d` default gate when `TINA_COMPLETE_PBR_GLTF_FIXTURE` is defined
- `GltfCookTests.CooksRepoCompletePbrFixture`

Not a production art pack. Games should ship their own cooked Catalog assets.
