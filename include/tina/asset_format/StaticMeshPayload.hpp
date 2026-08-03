#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <span>
#include <vector>

namespace Tina::AssetFormat {

// StaticMesh cooked payload schema v1 (little-endian, after CookedAsset header/deps).
// Product layouts support the original P3_N3_UV2 vertex and a tangent-bearing
// P3_N3_T4_UV2 vertex. The schema remains v1; vertexLayout selects the stride.
// Layout:
//   u16 schemaVersion (=1)
//   u16 vertexLayout  (1 = P3N3UV2, 2 = P3N3T4UV2)
//   u16 indexType     (1 = U16)
//   u16 submeshCount  (1..MaxSubmeshes)
//   u32 vertexCount
//   u32 indexCount
//   f32 boundsCenterX/Y/Z
//   f32 boundsRadius
//   StaticMeshSubmeshWire[submeshCount]  (16B each)
//   f32 vertices[vertexCount * stride]   // layout-defined interleaved vertices
//   u16 indices[indexCount]              // 2-byte aligned after vertices
//
// M11-E0 / product-3D foundation: format only (no glTF, no GPU upload in this header).
namespace StaticMeshWire {
inline constexpr Core::u16 SchemaVersion = 1;
inline constexpr Core::u32 HeaderBytes = 32;
inline constexpr Core::u32 SubmeshBytes = 16;
inline constexpr Core::u16 MaxSubmeshes = 64;
inline constexpr Core::u32 MaxVertexCount = 1'048'576;
inline constexpr Core::u32 MaxIndexCount = 4'194'304;
inline constexpr Core::u16 P3N3UV2FloatsPerVertex = 8;
inline constexpr Core::u16 P3N3T4UV2FloatsPerVertex = 12;
// Source-compatible aliases for callers that explicitly author the original layout.
inline constexpr Core::u16 FloatsPerVertex = P3N3UV2FloatsPerVertex;
inline constexpr Core::u32 BytesPerVertex = FloatsPerVertex * sizeof(float);
} // namespace StaticMeshWire

enum class StaticMeshVertexLayout : Core::u16 {
    Invalid = 0,
    P3N3UV2 = 1,
    P3N3T4UV2 = 2,
};

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
    StaticMeshVertexLayout vertexLayout = StaticMeshVertexLayout::P3N3UV2;
    StaticMeshIndexType indexType = StaticMeshIndexType::U16;
    float boundsCenterX = 0.0F;
    float boundsCenterY = 0.0F;
    float boundsCenterZ = 0.0F;
    float boundsRadius = 1.0F;
    std::span<const StaticMeshSubmeshDesc> submeshes{};
    // Interleaved floats selected by vertexLayout:
    // P3N3UV2:     [px,py,pz,nx,ny,nz,u,v]
    // P3N3T4UV2:   [px,py,pz,nx,ny,nz,tx,ty,tz,tw,u,v]
    std::span<const float> vertices{};
    std::span<const Core::u16> indices{};
};

struct StaticMeshPayloadView final {
    Core::u16 schemaVersion = 0;
    StaticMeshVertexLayout vertexLayout = StaticMeshVertexLayout::Invalid;
    StaticMeshIndexType indexType = StaticMeshIndexType::Invalid;
    Core::u16 submeshCount = 0;
    Core::u32 vertexCount = 0;
    Core::u32 indexCount = 0;
    float boundsCenterX = 0.0F;
    float boundsCenterY = 0.0F;
    float boundsCenterZ = 0.0F;
    float boundsRadius = 0.0F;
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
