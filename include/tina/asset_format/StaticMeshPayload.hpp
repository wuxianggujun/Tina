#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <span>
#include <vector>

namespace Tina::AssetFormat {

// StaticMesh cooked payload schema v2 (little-endian, after CookedAsset header/deps).
// Product vertices use the single P3_N3_T4_UV2 layout.
// Layout:
//   u16 schemaVersion (=2)
//   u16 vertexLayout  (=2, P3N3T4UV2)
//   u16 indexType     (1 = U16)
//   u16 submeshCount  (1..MaxSubmeshes)
//   u32 vertexCount
//   u32 indexCount
//   f32 boundsCenterX/Y/Z
//   f32 boundsRadius
//   u16 flags
//     bit0 = hasShaderOverride dependency (optional Shader AssetId)
//   u16 reserved
//   StaticMeshSubmeshWire[submeshCount]  (16B each)
//   f32 vertices[vertexCount * 12]       // interleaved P3N3T4UV2 vertices
//   u16 indices[indexCount]              // 2-byte aligned after vertices
// Shader dependency comes AFTER texture deps (if any) in CookedAsset dependency list.
//
// M11-E0 / product-3D foundation: format only (no glTF, no GPU upload in this header).
namespace StaticMeshWire {
inline constexpr Core::u16 SchemaVersion = 2;
inline constexpr Core::u32 HeaderBytes = 36;  // +4 for flags/reserved
inline constexpr Core::u32 SubmeshBytes = 16;
inline constexpr Core::u16 MaxSubmeshes = 64;
inline constexpr Core::u32 MaxVertexCount = 1'048'576;
inline constexpr Core::u32 MaxIndexCount = 4'194'304;
inline constexpr Core::u16 VertexLayout = 2;
inline constexpr Core::u16 FloatsPerVertex = 12;
inline constexpr Core::u32 BytesPerVertex = FloatsPerVertex * sizeof(float);
inline constexpr Core::u16 FlagHasShaderOverride = 1U << 0U;
inline constexpr Core::u16 KnownFlags = FlagHasShaderOverride;
} // namespace StaticMeshWire

enum class StaticMeshIndexType : Core::u16 {
    Invalid = 0,
    U16 = 1,
};

struct StaticMeshSubmeshDesc final {
    Core::u32 firstIndex = 0;
    Core::u32 indexCount = 0;
    Core::u32 materialSlot = 0;
    Core::u32 reserved = 0;
};

struct StaticMeshSubmeshView final {
    Core::u32 firstIndex = 0;
    Core::u32 indexCount = 0;
    Core::u32 materialSlot = 0;
    Core::u32 reserved = 0;
};

struct StaticMeshPayloadDesc final {
    StaticMeshIndexType indexType = StaticMeshIndexType::U16;
    float boundsCenterX = 0.0F;
    float boundsCenterY = 0.0F;
    float boundsCenterZ = 0.0F;
    float boundsRadius = 1.0F;
    std::span<const StaticMeshSubmeshDesc> submeshes{};
    // P3N3T4UV2: [px,py,pz,nx,ny,nz,tx,ty,tz,tw,u,v]
    std::span<const float> vertices{};
    std::span<const Core::u16> indices{};
    Core::AssetId shaderOverrideId{};  // optional Shader dependency
};

struct StaticMeshPayloadView final {
    Core::u16 schemaVersion = 0;
    StaticMeshIndexType indexType = StaticMeshIndexType::Invalid;
    Core::u16 submeshCount = 0;
    Core::u32 vertexCount = 0;
    Core::u32 indexCount = 0;
    float boundsCenterX = 0.0F;
    float boundsCenterY = 0.0F;
    float boundsCenterZ = 0.0F;
    float boundsRadius = 0.0F;
    bool hasShaderOverride = false;  // resolve via CookedAsset deps
    std::span<const StaticMeshSubmeshView> submeshes{};
    std::span<const float> vertices{};
    std::span<const Core::u16> indices{};

    [[nodiscard]] bool empty() const noexcept
    {
        return vertexCount == 0 || indexCount == 0 || vertices.empty() || indices.empty();
    }
};

[[nodiscard]] Core::Result<std::vector<std::byte>> writeStaticMeshPayloadBytes(const StaticMeshPayloadDesc& desc);

// Borrows payload bytes from a CookedAssetView / raw payload span.
// submeshes/vertices/indices views alias into `payload` storage.
[[nodiscard]] Core::Result<StaticMeshPayloadView> parseStaticMeshPayload(std::span<const std::byte> payload);

// Convenience: full cooked StaticMesh asset file (no dependencies).
[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedStaticMeshAsset(Core::AssetId assetId, const StaticMeshPayloadDesc& desc,
                           TargetPlatform platform = TargetPlatform::WindowsX64);

// Canonical unit cube matching bgfx Opaque3D fixture geometry (half-extent 1).
// Useful for product samples before glTF cook lands.
[[nodiscard]] StaticMeshPayloadDesc makeCanonicalUnitCubeMeshDesc(
    std::span<StaticMeshSubmeshDesc, 1> submeshStorage,
    std::span<float> vertexStorage,
    std::span<Core::u16> indexStorage) noexcept;

} // namespace Tina::AssetFormat
