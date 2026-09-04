#pragma once

#include <tina/asset/CookedAssetFile.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <optional>

namespace Tina::Asset {

// The Shader a cooked StaticMesh/SkinnedMesh names as its own default fragment stage, or
// nullopt when the mesh names none. Pure and owning nothing: the shader's lease, GPU owner
// and binding keys stay with ShaderBindingRegistry, and Mesh3DBindingRegistry keeps owning
// only mesh/material/texture. Callers turn this AssetId into a handle through
// AssetSystem::find and intern it through the shader registry.
//
// Selection is by AssetKind::Shader, never by position: parseCookedAssetView requires the
// cooked dependency stream to be strictly AssetId-sorted, so the override's index is decided
// by its id and no reader may take "the last dependency". The whole stream is scanned so that
// a mesh whose payload flag and dependency stream disagree is reported as the cook defect it
// is, in either direction.
[[nodiscard]] Core::Result<std::optional<Core::AssetId>>
readMesh3DShaderOverride(const CookedAssetFile& file) noexcept;

} // namespace Tina::Asset
