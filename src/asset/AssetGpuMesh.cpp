#include <tina/asset/AssetGpuMesh.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetTypedViews.hpp>

namespace Tina::Asset {

Core::Result<Render::GpuMeshId> uploadStaticMeshFromCooked(Render::IRenderDevice& device,
                                                           const CookedAssetFile& meshAsset)
{
    auto view = parseStaticMeshFromCooked(meshAsset);
    if (!view)
    {
        return Core::failure(std::move(view.error()).withContext("uploadStaticMeshFromCooked", "parse"));
    }
    if (view->empty())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "StaticMesh payload is empty");
    }
    switch (view->vertexLayout)
    {
    case AssetFormat::StaticMeshVertexLayout::P3N3UV2:
        return device.createStaticMeshP3N3UV2(Render::StaticMeshUploadDesc{
            .vertexCount = view->vertexCount,
            .indexCount = view->indexCount,
            .vertices = view->vertices,
            .indices = view->indices,
        });
    case AssetFormat::StaticMeshVertexLayout::P3N3T4UV2:
        return device.createStaticMeshP3N3T4UV2(Render::StaticMeshP3N3T4UV2UploadDesc{
            .vertexCount = view->vertexCount,
            .indexCount = view->indexCount,
            .vertices = view->vertices,
            .indices = view->indices,
        });
    default:
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "StaticMesh payload uses an unsupported vertex layout");
    }
}

Core::Status uploadAndBindStaticMeshForMeshKey(Render::IRenderDevice& device, const CookedAssetFile& meshAsset,
                                               Core::u32 meshKey)
{
    if (meshKey == 0)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "meshKey must be non-zero for mesh binding");
    }
    auto mesh = uploadStaticMeshFromCooked(device, meshAsset);
    if (!mesh)
    {
        return Core::failure(std::move(mesh.error()));
    }
    if (auto status = device.setMesh3DBinding(meshKey, *mesh); !status)
    {
        (void)device.destroyStaticMesh(*mesh);
        return status;
    }
    return Core::success();
}

} // namespace Tina::Asset
