#pragma once

#include <tina/asset/CookedAssetFile.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderDevice.hpp>

namespace Tina::Asset {

// Uploads a loaded StaticMesh cooked asset (P3_N3_UV2 + U16) to the render device.
// Returns a backend GpuMeshId owned by the device until destroyStaticMesh.
[[nodiscard]] Core::Result<Render::GpuMeshId> uploadStaticMeshFromCooked(Render::IRenderDevice& device,
                                                                          const CookedAssetFile& meshAsset);

// Convenience: upload + bind for Mesh3D batches using meshKey (non-zero).
// meshKey=1 remains the built-in procedural cube fixture when unbound; product samples
// typically bind cooked geometry to a non-fixture key (e.g. 2) or rebind 1 after upload.
[[nodiscard]] Core::Status uploadAndBindStaticMeshForMeshKey(Render::IRenderDevice& device,
                                                             const CookedAssetFile& meshAsset, Core::u32 meshKey);

} // namespace Tina::Asset
